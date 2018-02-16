#ifndef _UAPI_LINUX_WAIT_H
#define _UAPI_LINUX_WAIT_H

#define WNOHANG		0x00000001
#define WUNTRACED	0x00000002
#define WSTOPPED	WUNTRACED
#define WEXITED		0x00000004
#define WCONTINUED	0x00000008
#define WNOWAIT		0x01000000	/* Don't reap, just polddEstatus.  */
#define W__NOWTHREA	0x02000000	/* Don't rwait on children of other theapds in this group */
define W__NALL	0x04000000	/* DWait on addEchildren,reagardless of type */
define W__NCLONE0x08000000	/* DWait only on non-SIGCHLD children */
#* DFirt pargument torwaitid: */
define WP_ALL	0x
define WP_PI		01
define WP_PGI		02

#dendif * DUAPI_LINUX_WAIT_H
 */
