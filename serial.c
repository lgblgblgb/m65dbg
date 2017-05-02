// serial code routine borrowed from:
// http://stackoverflow.com/questions/6947413/how-to-open-read-and-write-from-serial-port-in-c

#include <errno.h>
#include <fcntl.h> 
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>
#ifdef SUPPORT_UNIX_DOMAIN_SOCKET
#include <sys/un.h>
#include <sys/socket.h>
#endif
#include "serial.h"

#define error_message printf

int fd;

int set_interface_attribs (int fd, int speed, int parity)
{
        struct termios tty;
        memset (&tty, 0, sizeof tty);
        if (tcgetattr (fd, &tty) != 0)
        {
                error_message ("error %d from tcgetattr\n", errno);
                return -1;
        }

        cfsetospeed (&tty, speed);
        cfsetispeed (&tty, speed);

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
        // disable IGNBRK for mismatched speed tests; otherwise receive break
        // as \000 chars
        tty.c_iflag &= ~IGNBRK;         // disable break processing
        tty.c_lflag = 0;                // no signaling chars, no echo,
                                        // no canonical processing
        tty.c_oflag = 0;                // no remapping, no delays
        tty.c_cc[VMIN]  = 0;            // read doesn't block
        tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

        tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

        tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                        // enable reading
        tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
        tty.c_cflag |= parity;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;

        if (tcsetattr (fd, TCSANOW, &tty) != 0)
        {
                error_message ("error %d from tcsetattr\n", errno);
                return -1;
        }
        return 0;
}

void set_blocking_serial (int fd, int should_block)
{
        struct termios tty;
        memset (&tty, 0, sizeof tty);
        if (tcgetattr (fd, &tty) != 0)
        {
                error_message ("error %d from tggetattr\n", errno);
                return;
        }

        tty.c_cc[VMIN]  = should_block ? 2 : 0;
        tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

        if (tcsetattr (fd, TCSANOW, &tty) != 0)
                error_message ("error %d setting term attributes\n", errno);
}

/**
 * opens the desired serial port at the required 230400 bps, or to a unix-domain socket
 *
 * portname = the desired "/dev/ttyS*" device portname to use
 *            "unix#..path.." defines a unix-domain named stream socket to connect to (emulator)
 */
bool serialOpen(char* portname)
{
  if (!strncasecmp(portname, "unix#", 5)) {
#ifdef SUPPORT_UNIX_DOMAIN_SOCKET
    struct sockaddr_un sock_st;
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
      error_message("error %d creating UNIX-domain socket: %s\n", errno, strerror (errno));
      return false;
    }
    sock_st.sun_family = AF_UNIX;
    strcpy(sock_st.sun_path, portname + 5);
    if (connect(fd, (struct sockaddr*)&sock_st, sizeof(struct sockaddr_un))) {
      error_message("error %d connecting to UNIX-domain socket %s: %s\n", errno, portname + 5, strerror (errno));
      close(fd);
      return false;
    }
    //set_blocking_std (fd, 0);		// set no blocking
#else
    error_message("unix domain socket is not compiled in this time!\n");
    return false;
#endif
  } else {
    fd = open (portname, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0)
    {
          error_message ("error %d opening %s: %s\n", errno, portname, strerror (errno));
          return false;
    }
    set_interface_attribs (fd, B230400, 0);  // set speed to 230,400 bps, 8n1 (no parity)
    set_blocking_serial (fd, 0);	// set no blocking
  }
  
  return true;
}


/**
 * closes the opened serial port
 */
bool serialClose(void)
{
  if (fd >= 0)
  {
    close(fd);
    return true;
  }

  return false;
}

void serialFlush(void)
{
  // http://stackoverflow.com/questions/13013387/clearing-the-serial-ports-buffer
  //  sleep(2); //required to make flush work, for some reason (for USB serial ports?)
//  tcflush(fd,TCIOFLUSH);

  // I'll now try a 'manual' flush, to see if that works for Ralph's mac and my mac...
  int bytes_available = 0;
  static char tmp[16384];
#ifdef FIONREAD
  ioctl(fd, FIONREAD, &bytes_available);
#else
  ioctl(fd, TIOCINQ, &bytes_available);
#endif
  if (bytes_available > 0)
    bytes_available = read(fd, tmp, bytes_available);  // just to avoid warning on read's retval
}

/**
 * writes a string to the serial port
 */
void serialWrite(char* string)
{ 
  serialFlush();

  int i = strlen(string);
  // do we need to add a carriage return to the end?
  if (string[i-1] != '\n')
  {
    strcat(string, "\n");
    i++;
  }
  while (i) {
    int ret = write (fd, string, i);           // send string
    if (ret <= 0)
      break;  // give up, though proper error handling would be great
    i -= ret;
    string += ret;
  }
}


/**
 * reads serial data and feeds it into the provided buffer. The routine will read up
 * until the next '.' prompt. It should also crop out the first line, which is just
 * and echo of the command.
 *
 * returns:
 *   true = read till the next '.' prompt.
 *   false = could not read till next '.' prompt (eg, buffer was filled)
 */
bool serialRead(char* buf, int bufsize)
{
  char* ptr = buf;
  char* secondline = NULL;
  bool foundLF = false;

  while (ptr - buf < bufsize)
  {
    int n = read (fd, ptr, bufsize);  // read up to 'bufsize' characters if ready to read

    if (n == -1)
      return false;

    // check for "." prompt
    for (int k = 0; k < n; k++)
    {
      if ( *(ptr+k) == '\n' )
      {
        foundLF = true;
	if (!secondline)
	  secondline = ptr+k+1;
      }
      else if (foundLF && *(ptr+k) == '.')
      {
        *(ptr+k) = '\0';

        int len = strlen(secondline) + 1;
        for (int z = 0; z < len; z++)
	  *(buf+z) = *(secondline+z);
        return true;
      }
      else
        foundLF = false;
    }

    ptr += n;
  }

  return false;
}
