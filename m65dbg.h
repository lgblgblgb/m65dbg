/* Edit configuration here! */

/* This seems to be needed for some prototypes ... */
#define _BSD_SOURCE

/* Compilation of support for UNIX domain socket target, used by Xemu's Mega-65 emulator
   LGB's note: you may or may not have problems with this on Windows /w cygwin, I have no idea ... 
   GI's note: I'm leaving this always enabled now, as I've gotten unix-sockets to work in winxp+cygwin */
#define SUPPORT_UNIX_DOMAIN_SOCKET

/* LGB's notes:
   If it's defined, attempt is made to compile my own HTTP/Websocket server implementation.
   (though it uses a 'freeware' SHA1 implementation).
   This is highly experimental code now, probably won't even compile. Its goal is to provide
   a web-enabled protocol so M65 board can be "bridged" with the web for debugging, and also
   I plan to have this protocol for Xemu as the "native" one later. */
//#define CONFIG_WEBSERVER

/* Only meaningful values with CONFIG_WEBSERVER defined */
#define UMON_PORT	4510
#define UMON_URL	"http://xemu.lgb.hu/umon/"
