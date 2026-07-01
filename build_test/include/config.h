#ifndef CONFIG_H
#define CONFIG_H

#ifndef VERSION
    #define VERSION "1.0.220"
#endif

#ifndef PACKAGE
    #define PACKAGE "DuneCity"
#endif

#define VERSIONSTRING   PACKAGE VERSION

/* #undef DUNELEGACY_DATADIR */

#ifndef CONFIGFILENAME
    #define CONFIGFILENAME "Dune City.ini"
#endif

#ifndef LOGFILENAME
    #define LOGFILENAME "Dune City.log"
#endif

/* #undef HAS_GETHOSTBYADDR_R */
/* #undef HAS_GETHOSTBYNAME_R */
/* #undef HAS_POLL */
/* #undef HAS_FCNTL */
/* #undef HAS_INET_PTON */
/* #undef HAS_INET_NTOP */
/* #undef HAS_MSGHDR_FLAGS */
/* #undef HAS_SOCKLEN_T */
/* #undef ENET_BUFFER_MAXIMUM */

#endif // CONFIG_H 
