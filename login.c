/*
 * $Id: login.c,v 1.97 1997-10-03 17:41:35 ghudson Exp $
 */

#ifndef lint
static char *rcsid = "$Id: login.c,v 1.97 1997-10-03 17:41:35 ghudson Exp $";
#endif

/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef lint
char copyright[] =
"@(#) Copyright (c) 1980 Regents of the University of California.\n\
 All rights reserved.\n";
#endif

#ifndef lint
static char sccsid[] = "@(#)login.c	5.15 (Berkeley) 4/12/86";
#endif

/*
 * login [ name ]
 * login -r hostname (for rlogind)
 * login -k hostname (for Kerberos rlogind with password access)
 * login -K hostname (for Kerberos rlogind with restricted access)
 * login -h hostname (for telnetd, etc.)
 */

#ifdef _BSD
/* causes header files to be screwed up */
#undef _BSD
#endif

#include <sys/types.h>
#include <sys/param.h>
#if !defined(VFS) || defined(_I386) || defined(ultrix)
#include <sys/quota.h>
#endif /* !VFS */
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/file.h>
#ifndef SYSV
#include <sys/dir.h>
#else
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <sys/fcntl.h>
#include <sys/ttold.h>
#include <sys/ttychars.h>
#include <sys/filio.h>
#include <sys/sysmacros.h>
#endif
#include <sys/wait.h>

#include <sgtty.h>
#include <utmp.h>
#ifdef SYSV
#include <utmpx.h>
#include <shadow.h>
#endif
#include <signal.h>
#include <pwd.h>
#include <stdio.h>
#include <lastlog.h>
#include <errno.h>
#ifndef SYSV
#include <ttyent.h>
#endif
#ifdef ultrix
#include <nsyslog.h>
#else
#include <syslog.h>
#endif
#include <string.h>
#include <krb.h>	
#include <netdb.h>
#include <netinet/in.h>
#include <grp.h>
#ifdef POSIX
#include <termios.h>
#endif

#ifdef KRB5
#include <krb5.h>
#endif

#ifdef ultrix
#include <sys/mount.h>
#include <sys/fs_types.h>
#endif

#ifdef SOLARIS
/* #define NGROUPS NGROUPS_MAX; see below */
#endif

/* Not sure about what this buys. */
#ifdef sgi
#define CRMOD  020
#endif

typedef struct in_addr inaddr_t;

#ifdef POSIX
#define sigtype void
#else
typedef int sigtype;
#endif

#define SETPAG

#undef NGROUPS
#define NGROUPS 16

#ifdef SETPAG
/* Allow for primary gid and PAG identifier */
#define MAX_GROUPS (NGROUPS-3)
#else
/* Allow for primary gid */
#define MAX_GROUPS (NGROUPS-1)
#endif

#define TTYGRPNAME	"tty"		/* name of group to own ttys */
#define TTYGID(gid)	tty_gid(gid)	/* gid that owns all ttys */

#ifndef PASSWD
#define PASSWD "/etc/passwd"
#endif

#ifndef PASSTEMP
#define PASSTEMP "/etc/ptmp"
#endif

#define	SCMPN(a, b)	strncmp(a, b, sizeof(a))
#define	SCPYN(a, b)	strncpy(a, b, sizeof(a))

#ifdef SYSV
#define NMAX	sizeof(utmpx.ut_name)
#define HMAX	sizeof(utmpx.ut_host)
#else
#define NMAX	sizeof(utmp.ut_name)
#define HMAX	sizeof(utmp.ut_host)
#endif

#ifndef FALSE
#define	FALSE	0
#define	TRUE	-1
#endif

#ifndef MAXBSIZE
#define MAXBSIZE 1024
#endif

#ifdef VFS
#define QUOTAWARN	"quota"	/* warn user about quotas */
#endif /* VFS */

#ifndef KRB_REALM
#define KRB_REALM	"ATHENA.MIT.EDU"
#endif

#define KRB_ENVIRON	"KRBTKFILE" /* Ticket file environment variable */
#define KRB_TK_DIR	"/tmp/tkt_" /* Where to put the ticket */

#ifdef KRB5
#define KRB5_ENVIRON	"KRB5CCNAME"
#define KRB5_TK_DIR	"/tmp/krb5cc_"
#endif

#define KRBTKLIFETIME	120	/* 10 hours */

#define PROTOTYPE_DIR	"/usr/athena/lib/prototype_tmpuser" /* Source for temp files */
#define TEMP_DIR_PERM	0755	/* Permission on temporary directories */

#define MAXPWSIZE   	128	/* Biggest key getlongpass will return */

#define START_UID	200	/* start assigning arbitrary UID's here */
#define MIT_GID		101	/* standard primary group "mit" */

char	nolog[] =	"/etc/nologin";
char	qlog[]  =	".hushlogin";
char	maildir[30] =	"/usr/spool/mail/";
char	lastlog[] =	"/usr/adm/lastlog";
char	inhibit[] =	"/etc/nocreate";
char	noattach[] =	"/etc/noattach";
char	noremote[] =	"/etc/noremote";
char	nocrack[] =	"/etc/nocrack";
char	go_register[] =	"/usr/etc/go_register";
char	get_motd[] =	"get_message";
char	rmrf[] =	"/bin/rm -rf";

/* uid, gid, etc. used to be -1; guess what setreuid does with that --asp */
#ifdef POSIX
struct  passwd nouser = {"        ",		/* name */
                             "nope",	/* passwd */
                             -2,	/* uid */
#ifdef ultrix
                             0,		/* pad */
#endif
                             -2,	/* gid */
#ifdef ultrix
                             0,		/* pad */
#endif
#ifdef _I386
			     "",	/* age */
#endif
                             0,		/* quota */
                             "",	/* comment */
                             "",	/* etc/gecos */
                             "",	/* dir */
                             "" };	/* shell */

struct  passwd newuser = {"\0\0\0\0\0\0\0\0",
                              "*",
                              START_UID,
#ifdef ultrix
                              0,
#endif
                              MIT_GID,
#ifdef ultrix
                              0,
#endif
#ifdef _I386
			      "",
#endif
                              0,
                              NULL,
                              NULL,
                              "/mit/\0\0\0\0\0\0\0\0",
                              NULL };
#else
struct	passwd nouser = {"", "nope", -2, -2, -2, "", "", "", "" };

struct	passwd newuser = {"\0\0\0\0\0\0\0\0", "*", START_UID, MIT_GID, 0,
			  NULL, NULL, "/mit/\0\0\0\0\0\0\0\0", NULL };
#endif /*POSIX*/

struct	sgttyb ttyb;
struct	utmp utmp;
#ifdef SYSV
    struct utmpx utmpx;
    char    term1[64];
    int second_time_around = 0;
    int hflag = 0;
#endif
char	minusnam[16] = "-";
char	*envinit[] = { 0 };		/* now set by setenv calls */
/*
 * This bounds the time given to login.  We initialize it here
 * so it can be patched on machines where it's too small.
 */
int	timeout = 60;

char	term[64];

struct	passwd *pwd;
struct	passwd *hes_getpwnam();
int	timedout();
char	*ttyname();
char	*crypt();
char	*getlongpass();
char	*stypeof();
extern	char **environ;
extern	int errno;

struct	tchars tc = {
	CINTR, CQUIT, CSTART, CSTOP, CEOT, CBRK
};
struct	ltchars ltc = {
	CSUSP, CDSUSP, CRPRNT, CFLUSH, CWERASE, CLNEXT
};

struct winsize win = { 0, 0, 0, 0 };

int	rflag=0;
int	kflag=0;
int 	Kflag=0;
int	usererr = -1;
int	krbflag = FALSE;	/* True if Kerberos-authenticated login */
#ifdef SETPAG
int	pagflag = FALSE;	/* True if we call setpag() */
#endif
int	tmppwflag = FALSE;	/* True if passwd entry is temporary */
int	tmpdirflag = FALSE;	/* True if home directory is temporary */
int	inhibitflag = FALSE;	/* inhibit account creation on the fly */
int	attachable = FALSE;	/* True if /etc/noattach doesn't exist */
int	attachedflag = FALSE;	/* True if homedir attached */
int	errorprtflag = FALSE;	/* True if login error already printed */
int	no_remote = FALSE;	/* True if /etc/noremote exists */
int	no_crack = FALSE;	/* True if /etc/nocrack exists */
int	shadow = TRUE;		/* True if using /etc/shadow */
char	rusername[NMAX+1], lusername[NMAX+1];
char	rpassword[NMAX+1];
char	name[NMAX+1];
char	*rhost;

AUTH_DAT *kdata = (AUTH_DAT *)NULL;

#ifdef SHADOW
#ifdef sgi
extern int _getpwent_no_shadow;
#endif

struct passwd *
get_pwnam(usr)
char *usr;
{
  struct passwd *pwd;
  struct spwd *sp;

  pwd = getpwnam(usr);
  sp = getspnam(usr);
  if ((sp != NULL) && (pwd != NULL))
    pwd->pw_passwd = sp->sp_pwdp;
  return(pwd);
}
#endif

main(argc, argv)
    char *argv[];
{
    register char *namep;
#ifdef SYSV
    int pflag = 0, t, f, c;
    char pasname[9];
#else
    int pflag = 0, hflag = 0, t, f, c;
#endif
    int invalid, quietlog, forkval;
    FILE *nlfd;
    char *ttyn, *tty, saltc[2];
    long salt;
    int ldisc = 0, zero = 0, found = 0, i, j;
    char **envnew;
    FILE *nrfd;
#ifdef POSIX
    struct termios tio;
#endif
#ifdef _I386
    struct stat 	pwdbuf;
#endif

#ifdef SYSV
    char ptty [10];
    char utmpx_tty[20];
    char *p;
    char new_id[20];
    char *tp;
    struct utmp *ut_tmp;
    struct utmpx *utx_tmp;
    char *tmp; /* temp pointer */
#endif


#ifdef POSIX
    struct sigaction sa;
    (void) sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = (void (*)())timedout;
    (void) sigaction(SIGALRM, &sa, (struct sigaction *)0);
    alarm(timeout);
    sa.sa_handler = SIG_IGN;
    (void) sigaction(SIGQUIT, &sa, (struct sigaction *)0);
    (void) sigaction(SIGINT, &sa, (struct sigaction *)0);
#else
    signal(SIGALRM, timedout);
    alarm(timeout);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGINT, SIG_IGN);
#endif
    setpriority(PRIO_PROCESS, 0, 0);
    umask(022);
#if !defined(VFS) || defined(ultrix)
    quota(Q_SETUID, 0, 0, 0);
#endif /* !VFS || ultrix */

    /*
     * -p is used by getty to tell login not to destroy the environment
     * -r is used by rlogind to cause the autologin protocol;
     * -k is used by klogind to cause the Kerberos autologin protocol;
     * -K is used by klogind to cause the Kerberos autologin protocol with
     *    restricted access.;
     * -h is used by other servers to pass the name of the
     * remote host to login so that it may be placed in utmp and wtmp
     */

    while (argc > 1) {
	if (strcmp(argv[1], "-d") == 0) {
	    /* This flag can be passed to the System V login to specify a tty
	     * name instead of calling ttyname().  Just ignore it. */
	    if (argv[2] == 0)
		exit(1);
	    argc -= 2;
	    argv += 2;
	}
	if (strcmp(argv[1], "-r") == 0) {
	    if (rflag || kflag || Kflag || hflag) {
		printf("Only one of -r -k -K or -h allowed\n");
		exit(1);
	    }
	    if (argv[2] == 0)
	      exit(1);
	    rflag = 1;
	    usererr = doremotelogin(argv[2]);
#ifdef SYSV
	    SCPYN(utmpx.ut_host, argv[2]);
#else
	    SCPYN(utmp.ut_host, argv[2]);
#endif
	    argc -= 2;
	    argv += 2;
	    continue;
	}
		if (strcmp(argv[1], "-k") == 0) {
			if (rflag || kflag || Kflag || hflag) {
				printf("Only one of -r -k -K or -h allowed\n");
				exit(1);
			}
			kflag = 1;
			usererr = doKerberosLogin(argv[2]);
#ifdef SYSV
			SCPYN(utmpx.ut_host, argv[2]);
#else
			SCPYN(utmp.ut_host, argv[2]);
#endif
			argc -= 2;
			argv += 2;
			continue;
		}
		if (strcmp(argv[1], "-K") == 0) {
			if (rflag || kflag || Kflag || hflag) {
				printf("Only one of -r -k -K or -h allowed\n");
				exit(1);
			}
			Kflag = 1;
			usererr = doKerberosLogin(argv[2]);
#ifdef SYSV
			SCPYN(utmpx.ut_host, argv[2]);
#else
			SCPYN(utmp.ut_host, argv[2]);
#endif
			argc -= 2;
			argv += 2;
			continue;
		}
	if (strcmp(argv[1], "-h") == 0 && getuid() == 0) {
#ifdef SYSV
          term1[0] = '\0';
          tmp = strchr(argv[3], '=');
          if (tmp != NULL) {
            strcpy(term1, tmp+1);
          }  
#endif
	    if (rflag || kflag || Kflag || hflag) {
		printf("Only one of -r -k -K or -h allowed\n");
		exit(1);
	    }
	    hflag = 1;
#ifdef SYSV
			SCPYN(utmpx.ut_host, argv[2]);
#else
			SCPYN(utmp.ut_host, argv[2]);
#endif
	    argc -= 2;
	    argv += 2;
	    continue;
	}
	if (strcmp(argv[1], "-p") == 0) {
	    argc--;
	    argv++;
	    pflag = 1;
	    continue;
	}
	break;
    }
    ioctl(0, TIOCLSET, &zero);
    ioctl(0, TIOCNXCL, 0);
    ioctl(0, FIONBIO, &zero);
    ioctl(0, FIOASYNC, &zero);
    ioctl(0, TIOCGETP, &ttyb);
    /*
     * If talking to an rlogin process,
     * propagate the terminal type and
     * baud rate across the network.
     */
    if (rflag || kflag || Kflag)
#ifdef SYSV
	doremoteterm(term, term1, &ttyb); 
#else
	doremoteterm(term,  &ttyb); 
#endif

#ifdef POSIX
    /* Now setup pty as AIX shells expect */
    (void)tcgetattr(0, &tio);
    
    tio.c_iflag |= (BRKINT|IGNPAR|ISTRIP|IXON|IXANY|ICRNL);
    tio.c_oflag |= (OPOST|TAB3|ONLCR);
    tio.c_cflag &= ~(CSIZE|CBAUD);
    tio.c_cflag |= (CS8|B9600|CREAD|HUPCL|CLOCAL);
    tio.c_lflag |= (ICANON|ISIG|ECHO|ECHOE|ECHOK);
    tio.c_cc[VINTR] = CINTR;
    tio.c_cc[VQUIT] = CQUIT;
    tio.c_cc[VERASE] = CERASE;
    tio.c_cc[VKILL] = CKILL;
    tio.c_cc[VEOF] = CEOF;
    tio.c_cc[VEOL] = CNUL;

    (void)tcsetattr(0, TCSANOW, &tio);
#else
    ttyb.sg_erase = CERASE;
    ttyb.sg_kill = CKILL;
    ioctl(0, TIOCSLTC, &ltc);
    ioctl(0, TIOCSETC, &tc);
    ioctl(0, TIOCSETP, &ttyb);
#endif
    for (t = getdtablesize(); t > 2; t--)
	close(t);
    ttyn = ttyname(0);
    if (ttyn == (char *)0 || *ttyn == '\0')
	ttyn = "/dev/tty??";
    tty = strrchr(ttyn, '/');
    if (tty == NULL)
	tty = ttyn;
    else
	tty++;
#ifdef LOG_ODELAY 
    openlog("login", LOG_ODELAY, LOG_AUTH);
#endif
    /* destroy environment unless user has asked to preserve it */
    /* (Moved before passwd stuff by asp) */
#ifndef NOPFLAG
#if 0
    if (!pflag)
	environ = envinit;
#endif
#endif

    /* set up environment, this time without destruction */
    /* copy the environment before setenving */
    /* AIX1.2 removes the INIT* variables */
    i = 0;
    while (environ[i] != NULL)
	i++;
    envnew = (char **) malloc(sizeof (char *) * (i + 1));
    for (j=0; i >= 0; i--) {
	if(!environ[i]) continue;
#ifdef _I386

	if(strncmp(environ[i], "INIT", 4) == 0) continue;
#endif
	envnew[j++] = environ[i];
    }
    envnew[j++] = NULL;
    environ = envnew;

    t = 0;
    invalid = FALSE;
    inhibitflag = !access(inhibit,F_OK);
    attachable = access(noattach, F_OK);
    no_remote = !access(noremote, F_OK);
    no_crack = !access(nocrack, F_OK);

    /* Use /etc/shadow on the SGI iff it exists.
       Use /etc/shadow on Solaris always.
       On other platforms, never. */
#ifdef sgi
    shadow = !access(SHADOW, F_OK);
    _getpwent_no_shadow = 1;
#else
#ifdef SOLARIS
    shadow = TRUE;
#else
    shadow = FALSE;
#endif
#endif

    do {
	    errorprtflag = 0;
	    ldisc = 0;
	found = 0;
	ioctl(0, TIOCSETD, &ldisc);
#ifdef SYSV
	SCPYN(utmpx.ut_name, "");
#else
	SCPYN(utmp.ut_name, "");
#endif

	/*
	 * Name specified, take it.
	 */
	if (argc > 1) {
#ifdef SYSV
	    SCPYN(utmpx.ut_name, argv[1]);
#else
	    SCPYN(utmp.ut_name, argv[1]);
#endif
	    argc = 0;
	}
	/*
	 * If remote login take given name,
	 * otherwise prompt user for something.
	 */
	if ((rflag || kflag || Kflag) && !invalid) {

#ifdef SHADOW
	    SCPYN(utmpx.ut_name, lusername);
	    if((pwd = get_pwnam(lusername)) == NULL) {
#else
	    SCPYN(utmp.ut_name, lusername);
	    if((pwd = getpwnam(lusername)) == NULL) {
#endif

		    pwd = &nouser;
		    found = 0;
	    } else found = 1;
	} else {
#ifdef SYSV
		found = getloginname(&utmpx);
                second_time_around = 1;
		if (utmpx.ut_name[0] == '-') {
#else
		found = getloginname(&utmp);
		if (utmp.ut_name[0] == '-') {
#endif

			puts("login names may not start with '-'.");
			invalid = TRUE;
			continue;
		}
	}

	invalid = FALSE;

	if (!strcmp(pwd->pw_shell, "/bin/csh") ||
	    !strcmp(pwd->pw_shell, "/bin/athena/tcsh")) {
	    ldisc = NTTYDISC;
	    ioctl(0, TIOCSETD, &ldisc);
	}
	/*
	 * If no remote login authentication and
	 * a password exists for this user, prompt
	 * for one and verify it.
	 */
	if (usererr == -1 && *pwd->pw_passwd != '\0') {
		/* we need to be careful to overwrite the password once it has
		 * been checked, so that it can't be recovered from a core image.
		 */

	    char *pp, pp2[MAXPWSIZE+1];
	    int krbval;
	    char tkfile[32];
#ifdef KRB5
	    char tk5file[32];
#endif
	    char realm[REALM_SZ];
	    
	    /* Set up the ticket file environment variable */
	    SCPYN(tkfile, KRB_TK_DIR);
	    strncat(tkfile, strchr(ttyn+1, '/')+1,
		    sizeof(tkfile) - strlen(tkfile));
	    while (pp = strchr((char *)tkfile + strlen(KRB_TK_DIR), '/'))
		*pp = '_';
	    (void) unlink (tkfile);
	    setenv(KRB_ENVIRON, tkfile, 1);
#ifdef KRB5
	    SCPYN(tk5file, KRB5_TK_DIR);
	    strncat(tk5file, strchr(ttyn+1, '/')+1,
		    sizeof(tk5file) - strlen(tk5file));
	    while (pp = strchr((char *)tk5file + strlen(KRB5_TK_DIR), '/'))
		*pp = '_';
	    (void) unlink (tk5file);
	    setenv(KRB5_ENVIRON, tk5file, 1);
#endif
	    
	    setpriority(PRIO_PROCESS, 0, -4);
	    pp = getlongpass("Password:");

	    if (!found) { /* check if we can create an entry */
	      if (inhibitflag) {
		invalid = TRUE;
	      } else if (no_remote && (hflag || rflag || kflag || Kflag)) {
		invalid = TRUE;
		fprintf(stderr, "You are not allowed to log in here.\n");
		if ((nrfd = fopen(noremote,"r")) != NULL) {
		  while ((c = getc(nrfd)) != EOF)
		    putchar(c);
		  fflush(stdout);
		  fclose(nrfd);
		}
		errorprtflag = TRUE;
		goto leavethis;
	      }
	      else /* we are allowed to create an entry */
		pwd = &nouser;
	    }

	    /* Modifications for Kerberos authentication -- asp */
	    SCPYN(pp2, pp);
	    pp[8]='\0';
	    if (found)
		    namep = crypt(pp, pwd->pw_passwd);
	    else {
		    salt = 9 * getpid();
		    saltc[0] = salt & 077;
		    saltc[1] = (salt>>6) & 077;
		    for (i=0;i<2;i++) {
			    c = saltc[i] + '.';
			    if (c > '9')
				    c += 7;
			    if (c > 'Z')
				    c += 6;
			    saltc[i] = c;
		    }
		    pwd->pw_passwd = namep = crypt(pp, saltc);
	    } 
			    
	    memset(pp, 0, MAXPWSIZE+1);	  /* No, Senator, I don't recall
					   anything of that nature ... */
	    setpriority(PRIO_PROCESS, 0, 0);

	    if (!invalid && (pwd->pw_uid != 0)) { 
#ifdef SETPAG
		/* We only call setpag() for non-root users */
		setpag();
		pagflag = TRUE;
#endif
		/* if not root, get Kerberos tickets */
		if(krb_get_lrealm(realm, 1) != KSUCCESS) {
		    SCPYN(realm, KRB_REALM);
		}
#ifdef SYSV
		strncpy(lusername, utmpx.ut_name, NMAX);
#else
		strncpy(lusername, utmp.ut_name, NMAX);
#endif
		lusername[NMAX] = '\0';
		krbval = krb_get_pw_in_tkt(lusername, "", realm,
				    "krbtgt", realm, KRBTKLIFETIME, pp2);
#ifdef KRB5
		if (krbval == 0) {
		    krb5_error_code krb5_ret;
		    char *etext;
		    
		    krb5_ret = do_v5_kinit(lusername, "", realm,
					   KRBTKLIFETIME,
					   pp2, 0, &etext);
		    if (krb5_ret && krb5_ret != KRB5KRB_AP_ERR_BAD_INTEGRITY) {
			com_err("login", krb5_ret, etext);
		    }
		}
#endif
		memset(pp2, 0, MAXPWSIZE+1);
		switch (krbval) {
		case INTK_OK:
			alarm(0);	/* Authentic, so don't time out. */
			if (verify_krb_tgt(realm) < 0) {
			    /* Oops.  He tried to fool us.  Tsk, tsk. */
			    invalid = TRUE;
			    goto leavethis;
			}
			invalid = FALSE;
			krbflag = TRUE;
			if (!found) {
				/* create a password entry: first ask the nameserver */
				/* to get us finger and shell info */
				struct passwd *nspwd;
				if ((nspwd = hes_getpwnam(lusername)) != NULL) {
					pwd->pw_uid = nspwd->pw_uid;
					pwd->pw_gid = nspwd->pw_gid;
					pwd->pw_gecos = nspwd->pw_gecos;
					pwd->pw_shell = nspwd->pw_shell;
					pwd->pw_dir = nspwd->pw_dir;
				} else {
				    invalid = TRUE;
				    goto leavethis;
				}
#ifdef SYSV
				memset(pasname, 0, sizeof(pasname));
				pwd->pw_name = &pasname[0];
				strncpy(pwd->pw_name, utmpx.ut_name, 8); 
#else
				strncpy(pwd->pw_name, utmp.ut_name, NMAX); 
#endif

				(void) insert_pwent(pwd);
				tmppwflag = TRUE;
			}
			chown(getenv(KRB_ENVIRON), pwd->pw_uid, pwd->pw_gid);
#ifdef KRB5
			chown(getenv(KRB5_ENVIRON), pwd->pw_uid, pwd->pw_gid);
#endif
			/* If we already have a homedir, use it.
			 * Otherwise, try to attach.  If that fails,
			 * try to create.
			 */
			tmpdirflag = FALSE;
			if (!goodhomedir()) {
				if (attach_homedir()) {
					puts("\nWarning: Unable to attach home directory.");
					if (make_homedir() >= 0) {
						puts("\nNOTE -- Your home directory is temporary.");
						puts("It will be deleted when this workstation deactivates.\n");
						tmpdirflag = TRUE;
					}
					else if (chdir("/") < 0) {
						printf("No directory '/'!\n");
						invalid = TRUE;
					} else {
						puts("Can't find or build home directory! Logging in with home=/");
						pwd->pw_dir = "/";
						tmpdirflag = FALSE;
					}
				}
				else {
					attachedflag = TRUE;
				} 
			}
			else
				puts("\nWarning: Using local home directory.");
			break;
		    
		  case KDC_NULL_KEY:
			invalid = TRUE;
			/* tell the luser to go register with kerberos */

			if (found)
				goto good_anyway;
			
			alarm(0);	/* If we are changing password,
					   he won't be logging in in this
					   process anyway, so we can reset */

			(void) insert_pwent(pwd);
			
			if (forkval = fork()) { /* parent */
			    if (forkval < 0) {
				perror("forking for registration program");
				sleep(3);
				exit(1);
			    }
			    while(wait(0) != forkval);
			    remove_pwent(pwd); 
			    exit(0);
			}
			/* run the passwd program as the user */
			setuid(pwd->pw_uid);
			
			execl(go_register, go_register, lusername, 0);
			perror("executing registration program");
			sleep(2);
			exit(1);
			/* These errors should be printed and are fatal */
		case KDC_PR_UNKNOWN:
		case KDC_PR_N_UNIQUE:
			invalid = TRUE;
			if (found)
				goto good_anyway;
		case INTK_BADPW:
			invalid = TRUE;
			errorprtflag = TRUE;
			fprintf(stderr, "%s\n",
				krb_err_txt[krbval]);
			goto leavethis;
		    /* These should be printed but are not fatal */
		case INTK_W_NOTALL:
		    invalid = FALSE;
		    krbflag = TRUE;
		    fprintf(stderr, "Kerberos error: %s\n",
			    krb_err_txt[krbval]);
			goto leavethis;
		  default:
		    fprintf(stderr, "Kerberos error: %s\n",
			    krb_err_txt[krbval]);
		    invalid = TRUE;
			errorprtflag = TRUE;
			goto leavethis;
		}
	} else { /* root logging in or inhibited; check password */
		memset(pp2, 0, MAXPWSIZE+1);
		invalid = TRUE;
	} 
	    /* if password is good, user is good */
    good_anyway:
	    invalid = invalid && strcmp(namep, pwd->pw_passwd);
    } 

leavethis:
	/*
	 * If our uid < 0, we must be a bogus user.
	 */
	if(pwd->pw_uid < 0) invalid = TRUE;
	/*
	 * If user not super-user, check for logins disabled.
	 */
	if (pwd->pw_uid != 0 && (nlfd = fopen(nolog, "r")) != 0) {
	    while ((c = getc(nlfd)) != EOF)
		putchar(c);
	    fflush(stdout);
	    sleep(5);
	    if (krbflag) {
		(void) dest_tkt();
#ifdef KRB5
		do_v5_kdestroy(0);
#endif
	    }
	    exit(0);
	}
#ifdef SYSLOG42
    openlog("login", 0);
#endif
	/*
	 * If valid so far and root is logging in,
	 * see if root logins on this terminal are permitted.
	 */
	if (!invalid && pwd->pw_uid == 0 && !rootterm(tty)) {
#ifdef SYSV
	    if (utmpx.ut_host[0])
#else
	    if (utmp.ut_host[0])
#endif
		syslog(LOG_CRIT,
		       "ROOT LOGIN REFUSED ON %s FROM %.*s",
#ifdef SYSV
		       tty, HMAX, utmpx.ut_host);
#else
		       tty, HMAX, utmp.ut_host);
#endif
	    else
		syslog(LOG_CRIT,
		       "ROOT LOGIN REFUSED ON %s", tty);
	    invalid = TRUE;
	}
	if (invalid) {
		if (!errorprtflag)
			printf("Login incorrect\n");
		if (++t >= 5) {
#ifdef SYSV
		if (utmpx.ut_host[0])
#else
		if (utmp.ut_host[0])
#endif
		    syslog(LOG_CRIT,
			   "REPEATED LOGIN FAILURES ON %s FROM %.*s, %.*s",
#ifdef SYSV
			   tty, HMAX, utmpx.ut_host,
			   NMAX, utmpx.ut_name);
#else
			   tty, HMAX, utmp.ut_host,
			   NMAX, utmp.ut_name);
#endif

		else
		    syslog(LOG_CRIT,
			   "REPEATED LOGIN FAILURES ON %s, %.*s",
#ifdef SYSV
			   tty, NMAX, utmpx.ut_name);
#else
			   tty, NMAX, utmp.ut_name);
#endif
		ioctl(0, TIOCHPCL, (struct sgttyb *) 0);
		close(0), close(1), close(2);
		sleep(10);
		exit(1);
	    }
	}
	if (!invalid && pwd->pw_shell && *pwd->pw_shell == '\0')
	    pwd->pw_shell = "/bin/sh";

	/* 
	  The effective uid is used under AFS for access.
	  NFS uses euid and uid for access checking
	 */
	setreuid(geteuid(),pwd->pw_uid);
	if (!invalid && chdir(pwd->pw_dir) < 0) {
	    if (chdir("/") < 0) {
		printf("No directory!\n");
		invalid = TRUE;
	    } else {
		puts("No directory! Logging in with home=/\n");
		pwd->pw_dir = "/";
	    }
	}
	setreuid(getuid(), getuid());
	/*
	 * Remote login invalid must have been because
	 * of a restriction of some sort, no extra chances.
	 */
	if (!usererr && invalid)
	    exit(1);

    } while (invalid);
    /* committed to login turn off timeout */
    alarm(0);
    if (tmppwflag) {
	    remove_pwent(pwd); 
	    insert_pwent(pwd); 
    } 
    if (!krbflag) puts("Warning: no Kerberos tickets obtained.");
    get_groups();
#if !defined(VFS) || defined(ultrix)
    if (quota(Q_SETUID, pwd->pw_uid, 0, 0) < 0 && errno != EINVAL) {
	if (errno == EUSERS)
	    printf("%s.\n%s.\n",
		   "Too many users logged on already",
		   "Try again later");
	else if (errno == EPROCLIM)
	    printf("You have too many processes running.\n");
	else
	    perror("quota (Q_SETUID)");
	sleep(5);
	if (krbflag) {
	    (void) dest_tkt();
#ifdef KRB5
	    do_v5_kdestroy(0);
#endif
	}
	exit(0);
    }
#endif /* VFS */
#ifdef _I386
    statx(pwd->pw_dir, &pwdbuf, sizeof(pwdbuf),0);
    quota(Q_DOWARN,pwd->pw_uid,pwdbuf.st_dev,0); 
#endif
#ifndef SYSV

    time(&utmp.ut_time);
#if !defined(_AIX)
    t = ttyslot();
    if (t > 0 && (f = open("/etc/utmp", O_WRONLY)) >= 0) {
	lseek(f, (long)(t*sizeof(utmp)), 0);
	SCPYN(utmp.ut_line, tty);
	write(f, (char *)&utmp, sizeof(utmp));
	close(f);
    }
#else
    strncpy(utmp.ut_id, tty, 6);
    utmp.ut_pid = getppid();
    utmp.ut_type = USER_PROCESS;
    if ((f = open("/etc/utmp", O_RDWR )) >= 0) {
	struct utmp ut_tmp;
	while (read(f, (char *) &ut_tmp, sizeof(ut_tmp)) == sizeof(ut_tmp))
	    if (ut_tmp.ut_pid == getppid())
		break;
	if (ut_tmp.ut_pid == getppid())
	    lseek(f, -(long) sizeof(ut_tmp), 1);
	strncpy(utmp.ut_id, ut_tmp.ut_id, 6);
	SCPYN(utmp.ut_line, tty);
	write(f, (char *)&utmp, sizeof(utmp));
	close(f);
    }
#endif
    if ((f = open("/usr/adm/wtmp", O_WRONLY|O_APPEND)) >= 0) {
	write(f, (char *)&utmp, sizeof(utmp));
	close(f);
    }
#else /* is SYSV */
/* Fill in the utmp/utmpx information */
    gettimeofday(&utmpx.ut_tv, NULL);
    if (strchr(ttyn, 'c') == NULL ) {
         strcpy(ptty, ttyn + 5);
         strcpy(utmpx_tty, ttyn);
      }
    else {
        strcpy(ptty, tty);
        strcpy(utmpx_tty, ptty);
    }
#ifdef SOLARIS	/* Here we search for /dev/pts/# */
    strcpy(utmpx.ut_line, utmpx_tty);
#else		/* But here it's pts/# */
    strcpy(utmpx.ut_line, ptty);
#endif
    utmpx.ut_type = USER_PROCESS;
    setutxent();
    utx_tmp = getutxline(&utmpx);
    strcpy(utmpx.ut_line, ptty);
    utmpx.ut_pid = getpid();
    if (utx_tmp) {
	memcpy(utmpx.ut_id, utx_tmp->ut_id, sizeof(utmpx.ut_id));
    } else {
	p = ptty + strlen(ptty);
	if (p > ptty && *(p - 1) != '/');
	    p--;
	if (p > ptty && *(p - 1) != '/');
	    p--;
	sprintf(new_id, "lo%s", p);
	memcpy(utmpx.ut_id, new_id, sizeof(utmpx.ut_id));
    }
    utmpx.ut_syslen = strlen(utmpx.ut_host);
    pututxline(&utmpx);
    getutmp(&utmpx, &utmp); 
    setutent();
/*  ut_tmp = getutline(&utmp); Doing this probably breaks Solaris,
	not doing it happens to work everywhere. You probably don't want
	to know. */
    pututline(&utmp);
    if ((f = open("/usr/adm/wtmp", O_WRONLY|O_APPEND)) >= 0) {
	write(f, (char *)&utmp, sizeof(utmp));
	close(f);
    }
    if ((f = open("/usr/adm/wtmpx", O_WRONLY|O_APPEND)) >= 0) {
	write(f, (char *)&utmpx, sizeof(utmpx));
	close(f);
    }
#endif
    quietlog = access(qlog, F_OK) == 0;
    if ((f = open(lastlog, O_RDWR)) >= 0) {
	struct lastlog ll;
	lseek(f, (long)pwd->pw_uid * sizeof (struct lastlog), 0);
	if (read(f, (char *) &ll, sizeof ll) == sizeof ll &&
	    ll.ll_time != 0 && !quietlog) {
	    printf("Last login: %.*s ",
		   24-5, (char *)ctime(&ll.ll_time));
	    if (*ll.ll_host != '\0')
	    printf("from %.*s\n",
		   sizeof (ll.ll_host), ll.ll_host);
	    else
	    printf("on %.*s\n",
		   sizeof (ll.ll_line), ll.ll_line);
	}
	lseek(f, (long)pwd->pw_uid * sizeof (struct lastlog), 0);
	time(&ll.ll_time);
	SCPYN(ll.ll_line, tty);
#ifdef SYSV
	SCPYN(ll.ll_host, utmpx.ut_host);
#else
	SCPYN(ll.ll_host, utmp.ut_host);
#endif
	write(f, (char *) &ll, sizeof ll);
	close(f);
    }
    chown(ttyn, pwd->pw_uid, TTYGID(pwd->pw_gid));

    if (!hflag && !rflag && !pflag && !kflag && !Kflag)		/* XXX */
	ioctl(0, TIOCSWINSZ, &win);
    chmod(ttyn, 0620);
    init_wgfile();

    /* Fork so that we can call kdestroy, notification server */
    dofork();
	
    setgid(pwd->pw_gid);
    strncpy(name, utmp.ut_name, NMAX);
    name[NMAX] = '\0';
    initgroups(name, pwd->pw_gid);
#ifndef VFS
    quota(Q_DOWARN, pwd->pw_uid, (dev_t)-1, 0);
#endif /* !VFS */

    /* This call MUST succeed */
    if(setuid(pwd->pw_uid) < 0) {
	perror("setuid");
	if (krbflag) {
	    (void) dest_tkt();
#ifdef KRB5
	    do_v5_kdestroy(0);
#endif
	}
	exit(1);
    }
    chdir(pwd->pw_dir);

    setenv("HOME", pwd->pw_dir, 1);
    setenv("SHELL", pwd->pw_shell, 1);
#ifdef SYSV
    if (term1[0] == '\0')
	setenv("TERM", "vt100", 0);
    else
	setenv("TERM", term1, 0); 
#else
    if (term[0] == '\0')
	strncpy(term, stypeof(tty), sizeof(term));
     setenv("TERM", term, 0);
#endif
    setenv("USER", pwd->pw_name, 1);

#ifdef sgi
    setenv("PATH", "/usr/athena/bin:/bin/athena:/usr/sbin:/usr/bsd:/sbin:/usr/bin:/bin:/usr/bin/X11", 1);
#else
#ifdef SOLARIS
    setenv("PATH", "/usr/athena/bin:/bin/athena:/bin:/usr/ucb:/usr/sbin:/usr/openwin/bin:/usr/ccs/bin", 1);
    setenv("LD_LIBRARY_PATH", "/usr/openwin/lib", 1);
#else
    setenv("PATH", "/usr/athena/bin:/bin/athena:/usr/ucb:/bin:/usr/bin", 1);
#endif /* SOLARIS */
#endif /* sgi */

#if defined(ultrix) && defined(mips)
    setenv("hosttype", "decmips", 1);
#endif
#ifdef SOLARIS
    setenv("hosttype", "sun4", 1);
#endif
#ifdef sgi
    setenv("hosttype", "sgi", 1);
#endif

    if ((namep = strrchr(pwd->pw_shell, '/')) == NULL)
	namep = pwd->pw_shell;
    else
	namep++;
    strcat(minusnam, namep);
    if (tty[sizeof("tty")-1] == 'd')
	syslog(LOG_INFO, "DIALUP %s, %s", tty, pwd->pw_name);
    if (pwd->pw_uid == 0)
#ifdef SYSV
	if (utmpx.ut_host[0])
#else
	if (utmp.ut_host[0])
#endif
			if (kdata) {
				syslog(LOG_NOTICE, "ROOT LOGIN via Kerberos from %.*s",
#ifdef SYSV
					HMAX, utmpx.ut_host);
#else
					HMAX, utmp.ut_host);
#endif
				syslog(LOG_NOTICE, "     (name=%s, instance=%s, realm=%s).",
					kdata->pname, kdata->pinst, kdata->prealm );
			} else {
				syslog(LOG_NOTICE, "ROOT LOGIN %s FROM %.*s",
#ifdef SYSV
					tty, HMAX, utmpx.ut_host);
#else
					tty, HMAX, utmp.ut_host);
#endif
			}
		else
			if (kdata) {
				syslog(LOG_NOTICE, "ROOT LOGIN via Kerberos %s ", tty);
				syslog(LOG_NOTICE, "     (name=%s, instance=%s, realm=%s).",
					kdata->pname,kdata->pinst,kdata->prealm);
			} else {
				syslog(LOG_NOTICE, "ROOT LOGIN %s", tty);
			}
    if (!quietlog) {
	struct stat st;

	showmotd();
#ifndef sgi /* Irix does a mail check in the system-wide dotfiles. */
	strcat(maildir, pwd->pw_name);
	if (stat(maildir, &st) == 0 && st.st_size != 0)
	    printf("You have %smail.\n",
		   (st.st_mtime > st.st_atime) ? "new " : "");
#endif
    }
#ifdef VFS
    switch(forkval = fork()) {
    case -1:
	printf("Unable to fork to run quota.\n");
	break;
    case 0:
	execlp(QUOTAWARN, "quota", 0);
	exit(1);
	/* NOTREACHED */
    default:
	while(wait(0) != forkval) ;
	break;
    }
#endif /* VFS */
#ifdef POSIX
    sa.sa_handler = SIG_DFL;
    (void) sigaction(SIGALRM, &sa, (struct sigaction *)0);
    (void) sigaction(SIGQUIT, &sa, (struct sigaction *)0);
    (void) sigaction(SIGINT, &sa, (struct sigaction *)0);
    sa.sa_handler = SIG_IGN;
    (void) sigaction(SIGTSTP, &sa, (struct sigaction *)0);
#else
    signal(SIGALRM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_IGN);
#endif

    execlp(pwd->pw_shell, minusnam, 0);
    perror(pwd->pw_shell);
    printf("No shell\n");
    if (krbflag) {
	(void) dest_tkt();
#ifdef KRB5
        do_v5_kdestroy(0);
#endif
    }
    exit(0);
}

getloginname(up)
	register struct utmp *up;
{
	register char *namep;
	int c;
	while (up->ut_name[0] == '\0') {
		namep = up->ut_name;
#ifdef SYSV
                if (hflag || second_time_around || rflag || kflag || Kflag)
#endif
		  printf("login: "); 
		while ((c = getchar()) != '\n') {
			if (c == ' ')
				c = '_';
			if (c == EOF)
				exit(0);
			if (namep < up->ut_name+NMAX)
				*namep++ = c;
		}
	}
	strncpy(lusername, up->ut_name, NMAX);
	lusername[NMAX] = 0;
#ifdef SHADOW
	if((pwd = get_pwnam(lusername)) == NULL) {
#else
	if((pwd = getpwnam(lusername)) == NULL) {
#endif
	    pwd = &nouser;
	    return(0);			/* NOT FOUND */
	}
	return(1);			/* FOUND */
}

timedout()
{

	printf("Login timed out after %d seconds\n", timeout);
	exit(0);
}

int	stopmotd;
catch()
{

#ifdef POSIX
      struct sigaction sa;
      (void) sigemptyset(&sa.sa_mask);
      sa.sa_flags = 0;
      sa.sa_handler = SIG_IGN;
      (void) sigaction(SIGINT, &sa, (struct sigaction *)0);
#else
      signal(SIGINT, SIG_IGN);
#endif
      stopmotd++;

}

rootterm(tty)
	char *tty;
{

#ifndef INITTAB
	register struct ttyent *t;

	if ((t = getttynam(tty)) != NULL) {
		if (t->ty_status & TTY_SECURE)
			return (1);
	}
	return (0);
#else 
	/* This is moot when /etc/inittab is used - there is no
	   per tty resource available */
	return (1);
#endif
}

showmotd()
{
	FILE *mf;
	register c;
	int forkval;

#ifdef POSIX
      struct sigaction sa;
      (void) sigemptyset(&sa.sa_mask);
      sa.sa_flags = 0;
      sa.sa_handler = (void (*)()) catch;
      (void) sigaction(SIGINT, &sa, (struct sigaction *)0);
#else
	signal(SIGINT, catch);
#endif
	if (forkval = fork()) { /* parent */
		if (forkval < 0) {
			perror("forking for motd service");
			sleep(3);
		}
		else {
		  while (wait(0) != forkval)
		    ;
	        }
	}
	else {
#ifndef sgi /* Irix shows the motd in the system-wide dotfiles. */
		if ((mf = fopen("/etc/motd", "r")) != NULL) {
			while ((c = getc(mf)) != EOF && stopmotd == 0)
				putchar(c);
			fclose(mf);
		}
#endif
		if (execlp(get_motd, get_motd, "-login", 0) < 0) {
			/* hide error code if any... */
			exit(0);
		}
	}
#ifdef POSIX
      sa.sa_handler = SIG_IGN;
      (void) sigaction(SIGINT, &sa, (struct sigaction *)0);
#else
	signal(SIGINT, SIG_IGN);
#endif
}

#undef	UNKNOWN
#define UNKNOWN "su"
#ifndef SYSV
char *
stypeof(ttyid)
	char *ttyid;
{
	register struct ttyent *t;

	if (ttyid == NULL || (t = getttynam(ttyid)) == NULL)
		return (UNKNOWN);
	return (t->ty_type);
}
#endif
doremotelogin(host)
	char *host;
{
	getstr(rusername, sizeof (rusername), "Remote user");
	getstr(lusername, sizeof (lusername), "Local user");
	getstr(term, sizeof(term), "Terminal type");
	if (getuid()) {
		pwd = &nouser;
		return(-1);
	}
#ifdef SHADOW
	if((pwd = get_pwnam(lusername)) == NULL) {
#else
	if((pwd = getpwnam(lusername)) == NULL) {
#endif
	    pwd = &nouser;
	    return(-1);
	}
	return(ruserok(host, (pwd->pw_uid == 0), rusername, lusername));
}

doKerberosLogin(host)
	char *host;
{
	int rc;
        struct hostent *hp = gethostbyname(host);
	struct sockaddr_in sin;
	KTEXT ticket;
	char instance[INST_SZ], version[9];

	/*
	 * Kerberos autologin protocol.
	 */

	(void) memset(&sin, 0, sizeof(sin));

        if (hp)
                (void) memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
        else
                /*
		 * No host addr prevents auth, so
                 * punt krb and require password
		 */
                if (Kflag) {
                        goto paranoid;
                } else {
			pwd = &nouser;
                        return(-1);
		}

	kdata = (AUTH_DAT *)malloc( sizeof(AUTH_DAT) );
	ticket = (KTEXT) malloc(sizeof(KTEXT_ST));

	strcpy(instance, "*");
	if (rc=krb_recvauth(0L, 0, ticket, "rcmd", instance, &sin,
			    NULL, kdata, NULL, NULL, version )) {
		printf("Kerberos rlogin failed: %s\r\n",krb_get_err_text(rc));
		if (Kflag) {
paranoid:
			/*
			 * Paranoid hosts, such as a Kerberos server, specify the Klogind
			 * daemon to disallow even password access here.
			 */
			printf("Sorry, you must have Kerberos authentication to access this host.\r\n");
			exit(1);
		}
	}
	getstr(lusername, sizeof (lusername), "Local user");
	getstr(term, sizeof(term), "Terminal type");
	if (getuid()) {
		pwd = &nouser;
		return(-1);
	}
#ifdef SHADOW
	pwd = get_pwnam(lusername);
#else
	pwd = getpwnam(lusername);
#endif
	if (pwd == NULL) {
		pwd = &nouser;
		return(-1);
	}

	/*
	 * if Kerberos login failed because of an error in GetKerberosData,
	 * return the indication of a bad attempt.  User will be prompted
	 * for a password.  We CAN'T check the .rhost file, because we need 
	 * the remote username to do that, and the remote username is in the 
	 * Kerberos ticket.  This affects ONLY the case where there is Kerberos 
	 * on both ends, but Kerberos fails on the server end. 
	 */
	if (rc) {
		return(-1);
	}

	if (rc=kuserok(kdata,lusername)) {
		printf("login: %s has not given you permission to login without a password.\r\n",lusername);
		if (Kflag) {
		  exit(1);
		}
		return(-1);
	}
	return(0);

}

getstr(buf, cnt, err)
	char *buf;
	int cnt;
	char *err;
{
	int ocnt = cnt;
	char *obuf = buf;
	char c;

	do {
		if (read(0, &c, 1) != 1)
			exit(1);
		if (--cnt < 0) {
			fprintf(stderr, "%s '%.*s' too long, %d characters maximum.\r\n",
				err, ocnt, obuf, ocnt-1);
			exit(1);
		}
		*buf++ = c;
	} while (c != 0);
}

char	*speeds[] =
    { "0", "50", "75", "110", "134", "150", "200", "300",
      "600", "1200", "1800", "2400", "4800", "9600", "19200", "38400" };
#define	NSPEEDS	(sizeof (speeds) / sizeof (speeds[0]))

#ifdef SYSV
doremoteterm(term, term1, tp)
	char *term, *term1;
#else
doremoteterm(term, tp)
	char *term;
#endif

	struct sgttyb *tp;
{
	register char *cp = strchr(term, '/'), **cpp;
	char *speed;
#ifdef SYSV
        strncpy(term1, term, cp-term);
        term1[cp-term + 1] = '\0';
#endif
	if (cp) {
		*cp++ = '\0';
		speed = cp;
		cp = strchr(speed, '/');
		if (cp)
			*cp++ = '\0';
		for (cpp = speeds; cpp < &speeds[NSPEEDS]; cpp++)
			if (strcmp(*cpp, speed) == 0) {
				tp->sg_ispeed = tp->sg_ospeed = cpp-speeds;
				break;
			}
	}
#ifdef sgi
	tp->sg_flags = ECHO|CRMOD|O_ANYP|XTABS;
#else
	tp->sg_flags = ECHO|CRMOD|ANYP|XTABS;
#endif
}

/* BEGIN TRASH
 *
 * This is here only long enough to get us by to the revised rlogin
 */
compatsiz(cp)
	char *cp;
{
	struct winsize ws;

	ws.ws_row = ws.ws_col = -1;
	ws.ws_xpixel = ws.ws_ypixel = -1;
	if (cp) {
		ws.ws_row = atoi(cp);
		cp = strchr(cp, ',');
		if (cp == 0)
			goto done;
		ws.ws_col = atoi(++cp);
		cp = strchr(cp, ',');
		if (cp == 0)
			goto done;
		ws.ws_xpixel = atoi(++cp);
		cp = strchr(cp, ',');
		if (cp == 0)
			goto done;
		ws.ws_ypixel = atoi(++cp);
	}
done:
	if (ws.ws_row != -1 && ws.ws_col != -1 &&
	    ws.ws_xpixel != -1 && ws.ws_ypixel != -1)
		ioctl(0, TIOCSWINSZ, &ws);
}
/* END TRASH */

#if !defined(ultrix)
/*
 * Set the value of var to be arg in the Unix 4.2 BSD environment env.
 * Var should NOT end in '='; setenv inserts it. 
 * (bindings are of the form "var=value")
 * This procedure assumes the memory for the first level of environ
 * was allocated using malloc.
 */

/* XXX -- We should use putenv() on POSIX systems */

setenv(var, value, clobber)
	char *var, *value;
{
	extern char **environ;
	int index = 0;
	int varlen = strlen(var);
	int vallen = strlen(value);

	for (index = 0; environ[index] != NULL; index++) {
		if (strncmp(environ[index], var, varlen) == 0) {
			/* found it */
			if (!clobber)
				return;
			environ[index] = malloc(varlen + vallen + 2);
			strcpy(environ[index], var);
			strcat(environ[index], "=");
			strcat(environ[index], value);
			return;
		}
	}
	environ = (char **) realloc(environ, sizeof (char *) * (index + 2));
	if (environ == NULL) {
		fprintf(stderr, "login: malloc out of memory\n");
		if (krbflag) {
		    (void) dest_tkt();
#ifdef KRB5
		    do_v5_kdestroy(0);
#endif
		}
		exit(1);
	}
	environ[index] = malloc(varlen + vallen + 2);
	strcpy(environ[index], var);
	strcat(environ[index], "=");
	strcat(environ[index], value);
	environ[++index] = NULL;
}
#endif

/*
 * This routine handles cleanup stuff, notification service, and the like.
 * It exits only in the child process.
 */
dofork()
{
    int child;
#ifdef POSIX
    struct sigaction sa;
     (void) sigemptyset(&sa.sa_mask);
     sa.sa_flags = 0;
#endif

    if(!(child=fork()))
	    return; /* Child process */

    /* Setup stuff?  This would be things we could do in parallel with login */
    chdir("/");	/* Let's not keep the fs busy... */
    
    
    /* If we're the parent, watch the child until it dies */
    while(wait(0) != child)
	    ;

    /* Cleanup stuff */

    /* Send a SIGHUP to everything in the process group, but not us.
     * Originally included to support Zephyr over rlogin/telnet
     * connections, but it has some general use, since any personal
     * daemon can setpgrp(0, getpgrp(getppid())) before forking to be
     * sure of receiving a HUP when the user logs out.
     *
     * Note that we are assuming that the shell will set its process
     * group to its process id. Our csh does, anyway, and there is no
     * other way to reliably find out what that shell's pgrp is.
     */

#ifdef POSIX
    sa.sa_handler = SIG_IGN;
    (void) sigaction(SIGHUP, &sa, (struct sigaction *)0);
#else
    signal(SIGHUP, SIG_IGN);
#endif
    if(-1 == killpg(child, SIGHUP))
      {
	/* EINVAL shouldn't happen (SIGHUP is a constant),
	 * ESRCH could, but we ignore it
	 * EPERM means something actually is wrong, so log it
	 * (in this case, the signal didn't get delivered but
	 * something might have wanted it...)
	 */
	if(errno == EPERM)
	  syslog(LOG_DEBUG,
		 "EPERM trying to kill login process group: child_pgrp %d",
		 child);
      }

    /* Run dest_tkt to destroy tickets */
    (void) dest_tkt();		/* If this fails, we lose quietly */
#ifdef KRB5
    do_v5_kdestroy(0);
#endif

#ifdef SETPAG
    /* Destroy any AFS tokens */
    if (pagflag)
	ktc_ForgetAllTokens();
#endif

    /* Detach home directory if previously attached */
    if (attachedflag)
	    (void) detach_homedir();

    if (tmppwflag)
	    if (remove_pwent(pwd)) 
		    puts("Couldn't remove password entry");

    /* Leave */
    exit(0);
}


tty_gid(default_gid)
int default_gid;
{
	struct group *getgrnam(), *gr;
	int gid = default_gid;
	
	gr = getgrnam(TTYGRPNAME);
	if (gr != (struct group *) 0)
		gid = gr->gr_gid;
    
	endgrent();
    
	return (gid);
}

char *
getlongpass(prompt)
char *prompt;
{
#ifdef sgi /* POSIX but not tested elsewhere */
	struct termios ttyb;
#else
	struct sgttyb ttyb;
#endif
	int flags;
	register char *p;
	register c;
	FILE *fi;
	static char pbuf[MAXPWSIZE+1];
#if !defined(sun)
	sigtype (*signal())();
#endif
	sigtype (*sig)();
#ifdef POSIX
      struct sigaction sa, osa;
      (void) sigemptyset(&sa.sa_mask);
      sa.sa_flags = 0;
#endif

	if ((fi = fdopen(open("/dev/tty", 2), "r")) == NULL)
		fi = stdin;
	else
		setbuf(fi, (char *)NULL);

#ifdef POSIX
      sa.sa_handler = SIG_IGN;
      (void) sigaction(SIGINT, &sa, &osa);
#else
	sig = signal(SIGINT, SIG_IGN);
#endif
#ifdef sgi /* POSIX but not tested */
	tcgetattr(fileno(fi),&ttyb);
	flags = ttyb.c_lflag;
	ttyb.c_lflag &=~ECHO;
	tcsetattr(fileno(fi),TCSANOW,&ttyb);
#else
	ioctl(fileno(fi), TIOCGETP, &ttyb);
	flags = ttyb.sg_flags;
	ttyb.sg_flags &= ~ECHO;
	ioctl(fileno(fi), TIOCSETP, &ttyb);
#endif
	fprintf(stderr, "%s", prompt); fflush(stderr);
	for (p=pbuf; (c = getc(fi))!='\n' && c!=EOF;) {
		if (p < &pbuf[MAXPWSIZE])
			*p++ = c;
	}
	*p = '\0';
	fprintf(stderr, "\n"); fflush(stderr);
#ifdef sgi /* POSIX but not tested */
	ttyb.c_lflag = flags;
	tcsetattr(fileno(fi),TCSANOW,&ttyb);
#else
	ttyb.sg_flags = flags;
	ioctl(fileno(fi), TIOCSETP, &ttyb);
#endif
#ifdef POSIX
	(void) sigaction(SIGINT, &osa, NULL);
#else
	signal(SIGINT, sig);
#endif
	if (fi != stdin)
		fclose(fi);
	pbuf[MAXPWSIZE]='\0';
	return(pbuf);
}

/* Attach the user's home directory if "attachable" is set.
 */
attach_homedir()
{
#ifdef SYSV
        int status;
#else
	union wait status;
#endif
	int attachpid;
	if (!attachable)
		return (1);
	chdir("/");	/* XXX This is a temproary hack to fix the
			 * fact that home directories sometimes do
			 * not get attached if the user types his
			 * password wrong the first time. Some how
			 * working direcotyr becomes the users home
			 * directory BEFORE we try to attach. and it
			 * of course fails.
			 */

	if (!(attachpid = fork())) {
		setuid(pwd->pw_uid);
		freopen("/dev/null","w",stdout);
		execl("/bin/athena/attach","attach","-q", lusername,0);
		exit (-1);
	} 
	while (wait(&status) != attachpid)
		;
#ifdef SYSV
	if (!status) {
#else
	if (!status.w_retcode) {
#endif
		chown(pwd->pw_dir, pwd->pw_uid, pwd->pw_gid);
		return (0);
	}
	return (1);
} 

/* Detach the user's home directory */
detach_homedir()
{
#ifdef SYSV
        int status;
#else
	union wait status;
#endif
	int pid;

#ifdef notdef
	int i;
	char *level;
	for (i=0;i<3;i++) {
#endif
		if (!(pid = fork())) {
			setuid(pwd->pw_uid);
			freopen("/dev/null","w",stdout);
			freopen("/dev/null","w",stderr);
			execl("/bin/athena/fsid", "fsid", "-quiet", "-unmap",
			      "-filsys", lusername, 0);
			exit (-1);
		} 
		while (wait(&status) != pid)
			;
#ifdef notdef
		if (status.w_retcode == DETACH_OK)
			return;
		level = "1";
		if (i == 1)
			level = "9";
		if (i == 2)
			level = "9";
		printf("Killing processes using %s with signal %s\n",
		       pwd->pw_dir,level);
		if (!(pid = fork())) {
			freopen("/dev/null","w",stdout);
			freopen("/dev/null","w",stderr);
			execl("/etc/athena/ofiles","ofiles","-k",
			      level,pwd->pw_dir,0);
			exit (-1);
		}
		while (wait(0) != pid)
			;
	}
#endif /* notdef */
	return;
#ifdef notdef
	printf("Couldn't detach home directory!\n");
#endif /* notdef */
}

isremotedir(dir)
char *dir;
{
#ifdef ultrix
#define REMOTEDONE
    struct fs_data sbuf;

    if (statfs(dir, &sbuf) < 0)
	return(TRUE);

    switch(sbuf.fd_req.fstype) {
    case GT_ULTRIX:
    case GT_CDFS:
	return(FALSE);
    }
    return(TRUE);
#endif
    
#if (defined(vax) || defined(ibm032) || defined(sun) || defined(sgi)) && !defined(REMOTEDONE)
#define REMOTEDONE
#if defined(vax) || defined(ibm032)
#define NFS_MAJOR 0xff
#endif
#if defined(sun) || defined(sgi)
#define NFS_MAJOR 130
#endif
    struct stat stbuf;
  
    if (stat(dir, &stbuf))
	return(TRUE);

    if (major(stbuf.st_dev) == NFS_MAJOR)
	return(TRUE);
    if (stbuf.st_dev == 0x0001)			/* AFS */
	return(TRUE);

    return(FALSE);
#endif

#ifndef REMOTEDONE
    ERROR --- ROUTINE NOT IMPLEMENTED ON THIS PLATFORM;
#endif
}

goodhomedir()
{
	DIR *dp;

	if (access(pwd->pw_dir,F_OK))
		return (0);

	if (isremotedir(pwd->pw_dir))
		return(0);


	dp = opendir(pwd->pw_dir);
	if (!dp)
		return (0);
	readdir(dp);
	readdir(dp);
	if (readdir(dp)) {
		closedir(dp);
		return (1);
	}
	closedir(dp);
	return (0);
}
	
/*
 * Make a home directory, copying over files from PROTOTYPE_DIR.
 * Ownership and group will be set to the user's uid and gid.  Default
 * permission is TEMP_DIR_PERM.  Returns 0 on success, -1 on failure.
 */
make_homedir()
{
    DIR *proto;
#ifdef POSIX
    struct dirent *dp;
#else
    struct direct *dp;
#endif
    char tempname[MAXPATHLEN+1];
    char buf[MAXBSIZE];
    char killdir[40]; /* > sizeof(rmrf) + " /tmp/username" */
    struct stat statbuf;
    int fold, fnew;
    int n;
    extern int errno;

    if (inhibitflag)
	    return (-1);
    
    strcpy(pwd->pw_dir,"/tmp/");
    strcat(pwd->pw_dir,lusername);
    setenv("TMPHOME", "", 1);

    /* Make sure there's not already a directory called pwd->pw_dir
       before we try to unlink it. This was OK under BSD, but it's
       a Bad Thing under Ultrix. */
    if (0 == lstat(pwd->pw_dir, &statbuf))
      {
	if ((statbuf.st_mode & S_IFMT) == S_IFDIR)
	  {
	    if (statbuf.st_uid == pwd->pw_uid)
	      return (0); /* user's old temp homedir, presumably */

	    /* Hm. This looks suspicious... Kill it. */
	    sprintf(killdir, "%s %s", rmrf, pwd->pw_dir);
	    system(killdir); /* Check for success at mkdir below. */
	  }
	else
	  unlink(pwd->pw_dir); /* not a dir - unlink is safe */
      }
    else
      if (errno != ENOENT) /* == ENOENT --> there was nothing there */
	{
	  puts("Error while retrieving status of temporary homedir.");
	  return(-1);
	}
	
    /* Make the home dir and chdir to it */
#ifdef SYSV
    if(mkdir(pwd->pw_dir, TEMP_DIR_PERM) < 0) {
#else
    if(mkdir(pwd->pw_dir) < 0) {
#endif
      puts("Error while creating temporary directory.");
      /* We want to die even if the error is that the directory
	 already exists - because it shouldn't. */
      return(-1);
    } 
    chown(pwd->pw_dir, pwd->pw_uid, pwd->pw_gid);
#ifndef SYSV
    chmod(pwd->pw_dir, TEMP_DIR_PERM);
#endif
    chdir(pwd->pw_dir);
    
    /* Copy over the proto files */
    if((proto = opendir(PROTOTYPE_DIR)) == NULL) {
	puts("Can't open prototype directory!");
	rmdir(pwd->pw_dir); /* This was unlink() before - WTF? */
	return(-1);
    }

    for(dp = readdir(proto); dp != NULL; dp = readdir(proto)) {
	/* Don't try to copy . or .. */
	if(!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) continue;

	/* Copy the file */
	SCPYN(tempname, PROTOTYPE_DIR);
	strcat(tempname, "/");
	strncat(tempname, dp->d_name, sizeof(tempname) - strlen(tempname) - 1);
	if(stat(tempname, &statbuf) < 0) {
	    perror(tempname);
	    continue;
	}
	/* Only copy plain files */
	if(!(statbuf.st_mode & S_IFREG)) continue;

	/* Try to open the source file */
	if((fold = open(tempname, O_RDONLY, 0)) < 0) {
	    perror(tempname);
	    continue;
	}

	/* Open the destination file */
	if((fnew = open(dp->d_name, O_WRONLY|O_CREAT|O_EXCL,
			statbuf.st_mode)) < 0) {
			    perror(dp->d_name);
			    continue;
			}

	/* Change the ownership */
	fchown(fnew, pwd->pw_uid, pwd->pw_gid);

	/* Do the copy */
	for (;;) {
	    n = read(fold, buf, sizeof buf);
	    if(n==0) break;
	    if(n<0) {
		perror(tempname);
		break;
	    }
	    if (write(fnew, buf, n) != n) {
		perror(dp->d_name);
		break;
	    }
	}
	close(fnew);
	close(fold);
    }
    return(0);
}

insert_pwent(pwd)
struct passwd *pwd;
{
    FILE *pfile;
    int cnt, fd;
#ifdef SHADOW
    long lastchg = DAY_NOW;
#endif

    while (getpwuid(pwd->pw_uid))
      (pwd->pw_uid)++;

#ifdef SYSV
    fd = lckpwdf();
#else
    cnt = 10;
    while (cnt-- > 0 &&
	   (fd = open(PASSTEMP, O_WRONLY|O_CREAT|O_EXCL, 0644)) < 0)
      sleep(1);
#endif
    if (fd == -1) {
	syslog(LOG_CRIT, "failed to lock /etc/passwd for insert");
/*	printf("Failed to add you to /etc/passwd\n"); a big lie */
    }

    if((pfile=fopen(PASSWD, "a")) != NULL) {
	fprintf(pfile, "%s:%s:%d:%d:%s:%s:%s\n",
		pwd->pw_name,
		shadow ? "x" : (no_crack ? "*" : pwd->pw_passwd),
		pwd->pw_uid,
		pwd->pw_gid,
		pwd->pw_gecos,
		pwd->pw_dir,
		pwd->pw_shell);
	fclose(pfile);
    }

#ifdef SHADOW
   if( shadow &&
       (pfile=fopen(SHADOW, "a")) != NULL) {
     fprintf(pfile,"%s:%s:%d::::::\n",
	     pwd->pw_name,
	     pwd->pw_passwd,
	     lastchg);
     fclose(pfile);
   }
#endif

#ifdef SYSV
    (void)ulckpwdf();
#else
    close(fd);
    unlink(PASSTEMP);
#endif
}

remove_pwent(pwd)
struct passwd *pwd;
{
    FILE *newfile;
    struct passwd *copypw;
    struct stat statb;
    int cnt, fd;
    int failure = 0;

#ifdef SYSV
    fd = lckpwdf();
    if (fd != -1)
      fd = open(PASSTEMP, O_WRONLY|O_CREAT|O_TRUNC, 0644);
#else
    cnt = 10;
    while (cnt-- > 0 &&
	   (fd = open(PASSTEMP, O_WRONLY|O_CREAT|O_EXCL, 0644)) < 0)
      sleep(1);
#endif
    if (fd == -1) {
	syslog(LOG_CRIT, "failed to lock /etc/passwd for remove");
	printf("Failed to remove you from /etc/passwd\n");
    } else if ((newfile = fdopen(fd, "w")) != NULL) {
	setpwent();
	while ((copypw = getpwent()) != 0)
	    if (copypw->pw_uid != pwd->pw_uid)
		    fprintf(newfile, "%s:%s:%d:%d:%s:%s:%s\n",
			    copypw->pw_name,
			    copypw->pw_passwd,
			    copypw->pw_uid,
			    copypw->pw_gid,
			    copypw->pw_gecos,
			    copypw->pw_dir,
			    copypw->pw_shell);
	endpwent();
	fclose(newfile);
	if (stat(PASSTEMP, &statb) != 0 || statb.st_size < 80) {
	    syslog(LOG_CRIT, "something stepped on /etc/ptmp");
	    printf("Failed to cleanup login\n");
	    failure = 1;
	} else
	  rename(PASSTEMP, PASSWD);
	if (stat(PASSWD, &statb) != 0 || statb.st_size < 80) {
	    syslog(LOG_CRIT, "something stepped on /etc/passwd");
	    printf("Failed to cleanup login\n");
	    sleep(12);
	    if (stat(PASSWD, &statb) != 0 || statb.st_size < 80) {
		syslog(LOG_CRIT, "/etc/passwd still empty, adding root");
		newfile = fopen(PASSWD, "w");
		if (shadow)
		  fprintf(newfile, "root:x:0:1:System PRIVILEGED Account:/:/bin/csh\n");
		else
		  fprintf(newfile, "root:*:0:1:System PRIVILEGED Account:/:/bin/csh\n");
		fclose(newfile);
	      }
	    failure = 1;
	  }
#ifdef SHADOW
	if (!failure)
	  /* We execute this code if all stages of removing the user
	     from /etc/passwd were successful. Otherwise, something
	     may be broken and we should give up right now. */
	    {
	      if (shadow)
		failure = remove_spwent(pwd);
	    }
#endif
#ifdef SYSV
      (void)ulckpwdf();
#endif
      return(failure);
    }

#ifdef SYSV
    (void)ulckpwdf();
#endif
    return(1);
}

#ifdef SHADOW
remove_spwent(pwd)
struct passwd *pwd;
{
    FILE *newfile;
    struct passwd *copypw;
    struct spwd   *copyspw;
    struct stat statb;
    int cnt, fd;
    int failure = 0;

#ifdef SYSV
    /* We don't lock here, assuming we've been called from remove_pwent
       and are operating under its lock. We do this so that, with
       respect to the lock, updating both /etc/passwd and /etc/shadow
       is atomic. */
    if (fd != -1)
      fd = open(SHADTEMP, O_WRONLY|O_CREAT|O_TRUNC, 0600);
#else
    cnt = 10;
    while (cnt-- > 0 &&
	   (fd = open(SHADTEMP, O_WRONLY|O_CREAT|O_EXCL, 0600)) < 0)
      sleep(1);
#endif
    if (fd < 0) {
	syslog(LOG_CRIT, "failed to lock /etc/shadow for remove");
	printf("Failed to remove you from /etc/shadow\n");
    } else if ((newfile = fdopen(fd, "w")) != NULL) {
        setspent();
	while ((copyspw = getspent()) != NULL)
	    if (strcmp(copyspw->sp_namp , pwd->pw_name)) {
		    fprintf(newfile, "%s:%s:%d::::::\n",
			    copyspw->sp_namp,
			    copyspw->sp_pwdp,
			    copyspw->sp_lstchg);
		  }
	endspent();
	fclose(newfile);
	if (stat(SHADTEMP, &statb) != 0 || statb.st_size < 80) {
	    syslog(LOG_CRIT, "something stepped on /etc/stmp");
	    printf("Failed to cleanup login\n");
	    failure = 1;
	} else
	  rename(SHADTEMP, SHADOW);
	if (stat(SHADOW, &statb) != 0 || statb.st_size < 80) {
	    syslog(LOG_CRIT, "something stepped on /etc/shadow");
	    printf("Failed to cleanup login\n");
	    sleep(12);
	    if (stat(SHADOW, &statb) != 0 || statb.st_size < 80) {
		syslog(LOG_CRIT, "/etc/shadow still empty, adding root");
		newfile = fopen(SHADOW, "w");
		fprintf(newfile, "root::6445::::::");
		fclose(newfile);
	    }
	    failure = 1;
	}
	return(failure);
    } else return(1);
}
#endif

get_groups()
{
	FILE *grin,*grout;
	char **cp,grbuf[4096],*ptr,*pwptr,*numptr,*lstptr;
	char *grname[MAX_GROUPS], *grnum[MAX_GROUPS];
	char grlst[4096],grtmp[4096],*tmpptr;
	int ngroups,i,cnt,nentries, namelen;
	
	if (inhibitflag)
		return;
	
	cp = (char **)hes_resolve(pwd->pw_name,"grplist");
	if (!cp || !*cp)
		return;

	cnt = 10;
	while (!access("/etc/gtmp",0) && --cnt)
		sleep(1);
	unlink("/etc/gtmp");
	
	grin = fopen("/etc/group","r");
	if (!grin) {
		fprintf(stderr,"Can't open /etc/group!\n");
		return;
	}
	grout = fopen("/etc/gtmp","w");
	if (!grout) {
		fprintf(stderr,"Can't open /etc/gtmp!\n");
		fclose(grin);
		return;
	}

	/* Parse up to MAX_GROUPS group names and gids out of cp[0]. */
	ptr = cp[0];
	ngroups = 0;
	while (ngroups < MAX_GROUPS) {
		grname[ngroups] = ptr;
		ptr = strchr(ptr, ':');
		if (!ptr)
			break;
		*ptr++ = 0;
		grnum[ngroups] = ptr;
		ngroups++;
		ptr = strchr(ptr, ':');
		if (ptr)
			*ptr++ = 0;
		else
			break;
	}

	/* Count the groups the user is currently in. */
	namelen = strlen(pwd->pw_name);
	nentries = 0;
	while (fgets(grbuf,sizeof grbuf,grin)) {
		/* Find the gid and user list. */
		ptr = strchr(grbuf, ':');
		if (!ptr)
			break;
		numptr = strchr(ptr + 1, ':');
		if (!numptr)
			break;
		numptr++;
		ptr = strchr(numptr, ':');
		if (!ptr)
			break;
		*ptr = 0;

		/* Now check if the user is in the user list. */
		while (ptr) {
			ptr++;
			if (!strncmp(pwd->pw_name, ptr, namelen) &&
			    (ptr[namelen] == ',' || ptr[namelen] == ' ' ||
			     ptr[namelen] == '\n')) {
				nentries++;
				break;
			}
			ptr = strchr(ptr, ',');
		}
	}

	rewind(grin);
	while (fgets(grbuf,sizeof grbuf,grin) != 0) {
		if (!*grbuf)
			break;
		grbuf[strlen(grbuf)-1] = '\0';
		pwptr = strchr(grbuf,':');
		if (!pwptr)
			continue;
		*pwptr++ = '\0';
		numptr = strchr(pwptr,':');
		if (!numptr)
			continue;
		*numptr++ = '\0';
		lstptr = strchr(numptr,':');
		if (!lstptr)
			continue;
		*lstptr++ = '\0';
		strcpy(grlst,lstptr);
		if (nentries < MAX_GROUPS) {
			for (i=0;i<ngroups;i++) {
				if (strcmp(grname[i],grbuf))
					continue;
				lstptr = grlst;
				while (lstptr) {
					strcpy(grtmp,lstptr);
					tmpptr = strchr(grtmp,',');
					if (tmpptr)
						*tmpptr = '\0';
					if (!strcmp(grtmp,pwd->pw_name)) {
						grname[i] = "*";
						break;
					}
					lstptr = strchr(lstptr,',');
					if (lstptr)
						lstptr++;
				}
				if (lstptr)
					break;
				strcat(grlst,",");
				strcat(grlst,pwd->pw_name);
				grname[i] = "*";
				nentries++;
				break;
			}
		}
		fprintf(grout,"%s:%s:%s:%s\n",grbuf,pwptr,numptr,grlst);
	}

	for (i=0;i<ngroups && nentries<MAX_GROUPS;i++) {
		if (strcmp(grname[i],"*")) {
			fprintf(grout,"%s:%s:%s:%s\n",grname[i],"*",
				grnum[i],pwd->pw_name);
			nentries++;
		}
	}

	fclose(grin);
	fclose(grout);
	rename("/etc/gtmp","/etc/group");
	unlink("/etc/gtmp");
}

#ifdef SOLARIS
init_wgfile()
{
        int fd;
        char wgfile[16];
        char *wgfile1;
	static char errbuf[1024];

	strcpy(wgfile, "/tmp/wg.XXXXXX");
	wgfile1 = mktemp(&wgfile[0]);
	sprintf(errbuf, "WGFILE=%s", wgfile);
        putenv(errbuf);
}
#else
init_wgfile()
{
	char *wgfile;

	wgfile = "/tmp/wg.XXXXXX";

	mktemp(wgfile);

	setenv("WGFILE",wgfile,1);
}
#endif

/*
 * Verify the Kerberos ticket-granting ticket just retrieved for the
 * user.  If the Kerberos server doesn't respond, assume the user is
 * trying to fake us out (since we DID just get a TGT from what is
 * supposedly our KDC).  If the rcmd.<host> service is unknown (i.e.,
 * the local srvtab doesn't have it), let her in.
 *
 * Returns 1 for confirmation, -1 for failure, 0 for uncertainty.
 */
int verify_krb_tgt (realm)
    char *realm;
{
    char hostname[MAXHOSTNAMELEN], phost[BUFSIZ];
    struct hostent *hp;
    KTEXT_ST ticket;
    AUTH_DAT authdata;
    unsigned long addr;
    static /*const*/ char rcmd[] = "rcmd";
    char key[8];
    int krbval, retval, have_keys;

    if (gethostname(hostname, sizeof(hostname)) == -1) {
	perror ("cannot retrieve local hostname");
	return -1;
    }
    strncpy (phost, krb_get_phost (hostname), sizeof (phost));
    phost[sizeof(phost)-1] = 0;
    hp = gethostbyname (hostname);
    if (!hp) {
	perror ("cannot retrieve local host address");
	return -1;
    }
    memcpy (&addr, hp->h_addr, sizeof (addr));
    /* Do we have rcmd.<host> keys? */
    have_keys = read_service_key (rcmd, phost, realm, 0, KEYFILE, key)
	? 0 : 1;
    krbval = krb_mk_req (&ticket, rcmd, phost, realm, 0);
    if (krbval == KDC_PR_UNKNOWN) {
	/*
	 * Our rcmd.<host> principal isn't known -- just assume valid
	 * for now?  This is one case that the user _could_ fake out.
	 */
	if (have_keys)
	    return -1;
	else
	    return 0;
    }
    else if (krbval != KSUCCESS) {
	printf ("Unable to verify Kerberos TGT: %s\n", krb_err_txt[krbval]);
#ifndef SYSLOG42
	syslog (LOG_NOTICE|LOG_AUTH, "Kerberos TGT bad: %s",
		krb_err_txt[krbval]);
#endif
	return -1;
    }
    /* got ticket, try to use it */
    krbval = krb_rd_req (&ticket, rcmd, phost, addr, &authdata, "");
    if (krbval != KSUCCESS) {
	if (krbval == RD_AP_UNDEC && !have_keys)
	    retval = 0;
	else {
	    retval = -1;
	    printf ("Unable to verify `rcmd' ticket: %s\n",
		    krb_err_txt[krbval]);
	}
#ifndef SYSLOG42
	syslog (LOG_NOTICE|LOG_AUTH, "can't verify rcmd ticket: %s;%s\n",
		krb_err_txt[krbval],
		retval
		? "srvtab found, assuming failure"
		: "no srvtab found, assuming success");
#endif
	goto EGRESS;
    }
    /*
     * The rcmd.<host> ticket has been received _and_ verified.
     */
    retval = 1;
    /* do cleanup and return */
EGRESS:
    memset (&ticket, 0, sizeof (ticket));
    memset (&authdata, 0, sizeof (authdata));
    return retval;
}

#ifdef KRB5
/*
 * This routine takes v4 kinit parameters and performs a V5 kinit.
 * 
 * name, instance, realm is the v4 principal information
 *
 * lifetime is the v4 lifetime (i.e., in units of 5 minutes)
 * 
 * password is the password
 *
 * ret_cache_name is an optional output argument in case the caller
 * wants to know the name of the actual V5 credentials cache (to put
 * into the KRB5_ENV_CCNAME environment variable)
 *
 * etext is a mandatory output variable which is filled in with
 * additional explanatory text in case of an error.
 * 
 */
krb5_error_code do_v5_kinit(name, instance, realm, lifetime, password,
			    ret_cache_name, etext)
	char	*name;
	char	*instance;
	char	*realm;
	int	lifetime;
	char	*password;
	char	**ret_cache_name;
	char	**etext;
{
	krb5_context context;
	krb5_error_code retval;
	krb5_principal me = 0, server = 0;
	krb5_ccache ccache = NULL;
	krb5_creds my_creds;
	krb5_timestamp now;
	krb5_flags options = KDC_OPT_FORWARDABLE | KDC_OPT_PROXIABLE;

	char *cache_name;

	*etext = 0;
	if (ret_cache_name)
		*ret_cache_name = 0;
	memset(&my_creds, 0, sizeof(my_creds));

	retval = krb5_init_context(&context);
	if (retval)
		return retval;

	cache_name = krb5_cc_default_name(context);
	krb5_init_ets(context);

	retval = krb5_425_conv_principal(context, name, instance, realm, &me);
	if (retval) {
		*etext = "while converting V4 principal";
		goto cleanup;
	}

	retval = krb5_cc_resolve(context, cache_name, &ccache);
	if (retval) {
		*etext = "while resolving ccache";
		goto cleanup;
	}

	retval = krb5_cc_initialize(context, ccache, me);
	if (retval) {
		*etext = "while initializing cache";
		goto cleanup;
	}

	retval = krb5_build_principal_ext(context, &server,
					  krb5_princ_realm(context,
							   me)->length,
					  krb5_princ_realm(context, me)->data,
					  KRB5_TGS_NAME_SIZE, KRB5_TGS_NAME,
					  krb5_princ_realm(context,
							   me)->length,
					  krb5_princ_realm(context, me)->data,
					  0);
	if (retval)  {
		*etext = "while building server name";
		goto cleanup;
	}

	retval = krb5_timeofday(context, &now);
	if (retval) {
		*etext = "while getting time of day";
		goto cleanup;
	}

	my_creds.client = me;
	my_creds.server = server;
	my_creds.times.starttime = 0;
	my_creds.times.endtime = now + lifetime*5*60;
	my_creds.times.renew_till = 0;

	retval = krb5_get_in_tkt_with_password(context, options, NULL, NULL,
					       NULL, password, ccache,
					       &my_creds, NULL);
	if (retval) {
		*etext = "while calling krb5_get_in_tkt_with_password";
		goto cleanup;
	}

	if (ret_cache_name) {
		*ret_cache_name = (char *) malloc(strlen(cache_name)+1);
		if (!*ret_cache_name) {
			retval = ENOMEM;
			goto cleanup;
		}
		strcpy(*ret_cache_name, cache_name);
	}

cleanup:
	if (me)
		krb5_free_principal(context, me);
	if (server)
		krb5_free_principal(context, server);
	if (ccache)
		krb5_cc_close(context, ccache);
	my_creds.client = 0;
	my_creds.server = 0;
	krb5_free_cred_contents(context, &my_creds);
	krb5_free_context(context);
	return retval;
}

krb5_error_code do_v5_kdestroy(cachename)
	char	*cachename;
{
	krb5_context context;
	krb5_error_code retval;
	krb5_ccache cache;

	retval = krb5_init_context(&context);
	if (retval)
		return retval;

	if (!cachename)
		cachename = krb5_cc_default_name(context);

	krb5_init_ets(context);

	retval = krb5_cc_resolve (context, cachename, &cache);
	if (retval) {
		krb5_free_context(context);
		return retval;
	}

	retval = krb5_cc_destroy(context, cache);

	krb5_free_context(context);
	return retval;
}
#endif /* KRB5 */
