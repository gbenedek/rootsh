/*

rootsh - a logging shell wrapper for root wannabes

Copyright (C) 2004 Gerhard Lausser

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "config.h"

#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <syslog.h>
#include <string.h>
#include <pwd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <regex.h>
#if HAVE_SYS_SELECT_H
#  include <sys/select.h>
#endif
#if HAVE_LIBGEN_H
#  include <libgen.h>
#endif
#if HAVE_PTY_H
#  include <pty.h>
#endif

#if HAVE_SYS_PARAM_H
#  include <sys/param.h>
#endif
#if HAVE_FCNTL_H 
#  include <fcntl.h>
#endif
#if HAVE_STROPTS_H
#  include <stropts.h>
#endif

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "getopt.h"
#endif

#if NEED_GETUSERSHELL_PROTO
/* 
//  solaris has no own prototypes for these three functions
*/
char *getusershell(void);
void setusershell(void);
void endusershell(void);
#endif
extern char **environ;
/*
//   our own functions 
*/
void finish(int);
char *whoami(void);
char *setupusername(void);
char *setupshell(void);
int setupusermode(void);
int createsessionid(void);
int beginlogging(void);
void dologging(char *, int);
void endlogging(void);
void version(void);
void usage(void);
#ifdef LOGTOSYSLOG
extern void write2syslog(const void *oBuffer, size_t oCharCount);
#endif
#ifndef HAVE_FORKPTY
pid_t forkpty(int *master,  char  *name,  struct  termios *termp, struct winsize *winp);
#endif

/* 
//  global variables 
*/
char progName[MAXPATHLEN];             /* used for logfile naming */
int masterPty;
FILE *logFile;
char logFileName[MAXPATHLEN - 7];
char closedLogFileName[MAXPATHLEN];
char sessionId[MAXPATHLEN + 11];
struct termios termParams, newTty;
struct winsize winSize;
char *userName;                        /* the name of the calling user */
char *runAsUser = NULL;
pid_t runAsUserPid;


int main(int argc, char **argv, char **envp) {
  char *shell, *dashShell;
  fd_set readmask;
  int n, nfd, childPid;
  int useLoginShell = 0;
  char buf[BUFSIZ];
  int c;
/*
char **ep;

for (ep = envp; *ep != NULL; ep++) {
  fprintf (stderr, "%s\n", *ep);
}
*/
  /* 
  //  this should be rootsh, but it could have been renamed 
  */
  strncpy(progName, argv[0], (MAXPATHLEN - 1));

  while (1) {
    static struct option long_options[] = {
        {"help", 0, 0, 'h'},
        {"version", 0, 0, 'V'},
        {"user", 1, 0, 'u'},
        {"initial", 0, 0, 'i'},
        {0, 0, 0, 0}
    };
    int option_index = 0;
    c = getopt_long (argc, argv, "hViu:",
        long_options, &option_index);
    if (c == -1)
      break;
    switch (c) {
      case 'h':
      case '?':
        usage();
        break;
      case 'V':
        version();
        break;
      case 'i':
        useLoginShell = 1;
        break;
      case 'u':
    runAsUser = strdup(optarg);
        break;
      default:
        usage ();
      }
  }

  if (! createsessionid()) {
    exit(EXIT_FAILURE);
  }

  if ((userName = setupusername()) == NULL) {
    exit(EXIT_FAILURE);
  }

  if (! setupusermode()) {
    exit(EXIT_FAILURE);
  }
  
  if ((shell = setupshell()) == NULL) {
    exit(EXIT_FAILURE);
  }

  if (! beginlogging()) {
    exit(EXIT_FAILURE);
  }

  /* 
  //  save original terminal parameters 
  */
  tcgetattr(STDIN_FILENO, &termParams);
  /*
  //  save original window size 
  */
  ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&winSize);
  
  /* 
  //  fork a child process, create a pty pair, 
  //  make the slave the controlling terminal,
  //  create a new session, become session leader 
  //  and attach filedescriptors 0-2 to the slave pty 
  */
  if ((childPid = forkpty(&masterPty, NULL, &termParams, &winSize)) < 0) {
    perror("fork");
    exit(EXIT_FAILURE);
  }
  if (childPid == 0) {
    /* 
    //  execute the shell 
    //  if rootsh was called with the -i parameter (initial login)
    //  then prepend the shell's basename with a dash.
    //  otherwise call it as interactive shell.
    */
    if (runAsUser) {
      if (useLoginShell) {
        execl(SUCMD, (strrchr(SUCMD, '/') + 1), "-", runAsUser, 0);
      } else {
        execl(SUCMD, (strrchr(SUCMD, '/') + 1), runAsUser, 0);
      }
      perror(SUCMD);
    } else {
      if (useLoginShell) {
        dashShell = strdup(shell);
        dashShell = strrchr(dashShell, '/');
        dashShell[0] = '-';
        execl(shell, dashShell, 0);
      } else {
        execl(shell, (strrchr(shell, '/') + 1), "-i", 0);
      }
      perror(shell);
    }
    perror(progName);
  } else {
    /* 
    //  handle these signals (posix functions preferred) 
    */
#if defined(HAVE_SIGACTION)
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = finish;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);
#elif defined(HAVE_SIGSET)
    sigset(SIGINT, finish);
    sigset(SIGQUIT, finish);
#else
    signal(SIGINT, finish);
    signal(SIGQUIT, finish);
#endif
    newTty = termParams;
    /* 
    //  let read not return until at least 1 byte has been received 
    */
    newTty.c_cc[VMIN] = 1; 
    newTty.c_cc[VTIME] = 0;
    /* 
    //  don't perform output processing
    */
    newTty.c_oflag &= ~OPOST;
    /* 
    //  noncanonical input |signal characters off|echo off 
    */
    newTty.c_lflag &= ~(ICANON|ISIG|ECHO);
    /* 
    //  NL to CR off|don't ignore CR|CR to NL off|
    //  case sensitive input|no output flow control 
    */
    newTty.c_iflag &= ~(INLCR|IGNCR|ICRNL|
    /* 
    //  FreeBSD doesnt know this one 
    */
#ifdef IUCLC 
        IUCLC|
#endif
        IXON);

    /* 
    //  set the new tty modes 
    */
    if (tcsetattr(0, TCSANOW, &newTty) < 0) {
        perror("tcsetattr: stdin");
        exit(EXIT_FAILURE);
    }
    /* 
    //  now just sit in a loop reading from the keyboard and
    //  writing to the pseudo-tty, and reading from the  
    //  pseudo-tty and writing to the screen and the logfile.
    */
    for (;;) {
      /* 
      //  watch for users terminal and master pty to change status 
      */
      FD_ZERO(&readmask);
      FD_SET(masterPty, &readmask);
      FD_SET(0, &readmask);
      nfd = masterPty + 1;

      /* 
      //  wait for something to read 
      */
      n = select(nfd, &readmask, (fd_set *) 0, (fd_set *) 0,
        (struct timeval *) 0);
      if (n < 0) {
        perror("select");
        exit(EXIT_FAILURE);
      }

      /* 
      //  the user typed something... 
      //  read it and pass it on to the pseudo-tty 
      */
      if (FD_ISSET(0, &readmask)) {
        if ((n = read(0, buf, sizeof(buf))) < 0) {
          perror("read: stdin");
          exit(EXIT_FAILURE);
        }
        if (n == 0) {
          /* 
          //  the user typed end-of-file; we're done 
          */
          finish(0);
        }
        if (write(masterPty, buf, n) != n) {
          perror("write: pty");
          exit(EXIT_FAILURE);
        }
      }

      /* 
      //  there's output on the pseudo-tty... 
      //  read it and pass it on to the screen and the script file 
      //  echo is on, so we see here also the users keystrokes 
      */
      if (FD_ISSET(masterPty, &readmask)) {
        /* 
        //  the process died 
        */
        if ((n = read(masterPty, buf, sizeof(buf))) <= 0) {
          finish(0);
        } else {
          dologging(buf, n);
          write(STDIN_FILENO, buf, n);
        }
      }
    }
  }
  exit(EXIT_SUCCESS);
}



/* 
//  do some cleaning after the child exited 
//  this is the signal handler for SIGINT and SIGQUIT 
*/

void finish(int sig) {
  char msgbuf[BUFSIZ];
  int msglen;

  /* restore original tty modes */
  if (tcsetattr(0, TCSANOW, &termParams) < 0)
      perror("tcsetattr: stdin");
  if (sig == 0) {
    msglen = snprintf(msgbuf, (sizeof(msgbuf) - 1),
        "\n*** %s session ended by user\n", basename(progName));
  } else {
    msglen = snprintf(msgbuf, (sizeof(msgbuf) - 1),
        "\n*** %s session interrupted by signal %d\n", basename(progName), sig);
  }
  dologging(msgbuf, msglen);
  endlogging();
  close(masterPty);
  exit(EXIT_SUCCESS);
}



/* 
//  create a session identifier which helps you identify the lines
//  which belong to the same session when browsing large logfiles.
//  also used in the logfiles name.
//  for each host there may be no sessions running at the same time
//  having the same identifier.
*/

int createsessionid(void) {
  pid_t pid;

  /* 
  //  the process number is always unique
  */
  pid = getpid();
  sprintf(sessionId, "%s-%04x", basename(progName), pid);
  return(1);
}
  


/* 
//  open a logfile and initialize syslog. 
//  generate a random session identifier.
*/

int beginlogging(void) {
  time_t now;
  char msgbuf[BUFSIZ];
#ifdef LOGTOFILE
  int sec, min, hour, day, month, year, msglen;
#endif

#ifdef LOGTOFILE
  /*
  //  Construct the logfile name. 
  //  LOGDIR/<username>.YYYY.MM.DD.HH.MI.SS.<sessionId>
  //  When the session is over, the logfile will be renamed 
  //  to <logfile>.closed.
  //  If we don't log to a file at all, don't mention 
  //  a filename in the syslog logs.
  */
  now = time(NULL);
  year = localtime(&now)->tm_year + 1900;
  month = localtime(&now)->tm_mon + 1;
  day = localtime(&now)->tm_mday;
  hour = localtime(&now)->tm_hour;
  min = localtime(&now)->tm_min;
  sec = localtime(&now)->tm_sec;
  snprintf(logFileName, (sizeof(logFileName) - 1), 
      "%s/%s.%04d%02d%02d%02d%02d%02d.%s", 
       LOGDIR, userName, year,month, day, hour, min, sec,
       strrchr(sessionId, '-') + 1);
  snprintf(closedLogFileName, (sizeof(closedLogFileName) - 1),
      "%s.closed", logFileName);
  /* 
  //  Open the logfile 
  */
  if ((logFile = fopen(logFileName, "w")) == NULL) {
    perror(logFileName);
    return(0);
  }
  /* 
  //  Note the start time in the log file 
  */
  msglen = snprintf(msgbuf, (sizeof(msgbuf) - 1),
      "%s session opened for %s as %s on %s at %s", 
       basename(progName), userName, runAsUser ? runAsUser : getpwuid(getuid())->pw_name, ttyname(0), ctime(&now)); 
  fwrite(msgbuf, sizeof(char), msglen, logFile);
  fflush(logFile);
#endif
#ifdef LOGTOSYSLOG
  /* 
  //  Prepare usage of syslog with sessionid as prefix 
  */
  openlog(sessionId, LOG_NDELAY, SYSLOGFACILITY);
  /* 
  //  Note the log file name in syslog if there is one
  */
  syslog(SYSLOGFACILITY | SYSLOGPRIORITY, 
#ifdef LOGTOFILE
      "%s=%s,%s: logging new session (%s) to %s", 
      userName, runAsUser ? runAsUser : getpwuid(getuid())->pw_name, ttyname(0), sessionId, logFileName);
#else
      "%s=%s,%s: logging new session (%s)", 
      userName, runAsUser ? runAsUser : getpwuid(getuid())->pw_name, ttyname(0), sessionId);
#endif
#endif
  return(1);
}


/*
//  Send a buffer full of output to the selected logging destinations
//  Either to a local logfile or to the syslog server or both
*/

void dologging(char *msgbuf, int msglen) {
#ifdef LOGTOFILE
          fwrite(msgbuf, sizeof(char), msglen, logFile);
          fflush(logFile);    
#endif
#ifdef LOGTOSYSLOG
          write2syslog(msgbuf, msglen);
#endif    
}


/* 
//  send a final cr-lf to flush the log.
//  close the logfile and syslog.
//  append ".closed" to the logfile's name.
*/

void endlogging() {
  time_t now;
  int msglen;
  char msgbuf[BUFSIZ];

#ifdef LOGTOSYSLOG
  write2syslog("\r\n", 2);
  syslog(SYSLOGFACILITY | SYSLOGPRIORITY, "%s,%s: closing %s session (%s)", 
      userName, ttyname(0), basename(progName), sessionId);
  closelog();
#endif
#ifdef LOGTOFILE
  now = time(NULL);
  msglen = snprintf(msgbuf, (sizeof(msgbuf) - 1),
      "%s session closed for %s on %s at %s", basename(progName),
      userName, ttyname(0), ctime(&now)); 
  fwrite(msgbuf, sizeof(char), msglen, logFile);
  fclose(logFile);
  rename(logFileName, closedLogFileName);
#endif
}

  

/*
//  find out the username of the calling user
*/

char *setupusername() {
  char *userName = NULL;
  struct stat ttybuf;
  if((userName = getlogin()) == NULL) {
    /* 
    //  HP-UX returns NULL here so we take the controlling terminal's owner 
    */
    if(ttyname(0) != NULL) {
      if (stat(ttyname(0), &ttybuf) == 0) {
        if ((userName = getpwuid(ttybuf.st_uid)->pw_name) == NULL) {
          fprintf(stderr, "i don\'t know who you are\n");
        }
      }
    } else {
      /* 
      //  rootsh must be run interactively 
      */
      fprintf(stderr, "i don\'t know who you are\n");
    }
  }
  return userName;
}



/* 
//  find out which shell to use. return the pathname of the shell 
//  or NULL if an error occurred 
*/ 

char *setupshell() {
  int isvalid = 0;
  char *shell;
  char *validshell;
  
  /* 
  //  try to get the users current shell with two methods 
  */
  if ((shell = getenv("SHELL")) == NULL) {
    shell = getpwnam(userName)->pw_shell;
  }
  if (shell == NULL) {
    fprintf(stderr, "could not determine a valid shell\n");
#ifdef LOGTOSYSLOG
    syslog(SYSLOGFACILITY | LOG_WARNING, "%s,%s: has no valid shell", 
        userName, ttyname(0));
#endif
  } else {
    /* 
    //  compare it to the allowed shells in /etc/shells 
    */
    for (setusershell(), validshell = getusershell(); validshell && ! isvalid; validshell = getusershell()) {
      if (strcmp(validshell, shell) == 0) {
        isvalid = 1;
      }
    }
    endusershell();

    /* 
    //  do not allow invalid shells 
    */
    if (isvalid == 0) {
      fprintf(stderr, "%s is not in /etc/shells\n", shell);
#ifdef LOGTOSYSLOG
      syslog(SYSLOGFACILITY | LOG_WARNING, "%s,%s: %s is not in /etc/shells", 
          userName, ttyname(0), shell);
#endif
      return(NULL);
    }        
  }
  return(shell);
}


/*
//  if a username was given on the command line via -u 
//  see, if it has an acceptable length (yes, some have 64 character usernames)
//  see, if it exists. 
//  get the uid. 
//  clean up the environment
//  if not, forget this and run as root
*/

int setupusermode(void) {
  struct passwd *pass;

#ifndef SUCMD
  fprintf(stderr, "user mode is not possible with this version of %s\n", progName);
  return(1);
#endif
  if (runAsUser == NULL) {
    return(1);
  } else if (strlen(runAsUser) > 64) {
    fprintf(stderr, "this username is too long. i don\'t trust you\n");
    return(0);
  } else {
    if ((pass = getpwnam(runAsUser)) == NULL) {
      fprintf(stderr, "user %s does not exist\n", runAsUser);
      return(0);
    } else {
      runAsUserPid = pass->pw_uid;
      clearenv();
      return(1);
    }
  }
}



#ifndef HAVE_FORKPTY
/* 
//  emulation of the BSD function forkpty 
*/
#ifndef MASTERPTYDEV
#  error you need to specify a master pty device
#endif
pid_t forkpty(int *amaster,  char  *name,  struct  termios *termp, struct winsize *winp) {
  struct termios currentterm;
  struct winsize currentwinsize;
  pid_t pid;
  int master, slave;
  char *slavename;
  char *mastername = MASTERPTYDEV;

  /* 
  //  get current settings if termp was not provided by the caller 
  */
  if (termp == NULL) {
    tcgetattr(STDIN_FILENO, &currentterm);
    termp = &currentterm;
  }

  /* 
  //  same for window size 
  */
  if (winp == NULL) {
    ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&currentwinsize);
    winp->ws_row = currentwinsize.ws_row;
    winp->ws_col = currentwinsize.ws_col;
    winp->ws_xpixel = currentwinsize.ws_xpixel;
    winp->ws_ypixel = currentwinsize.ws_ypixel;
  }

  /*
  //  get a master pseudo-tty 
  */
  if ((master = open(mastername, O_RDWR)) < 0) {
    perror(mastername);
    return(-1);
  }

  /*
  //  set the permissions on the slave pty 
  */
  if (grantpt(master) < 0) {
    perror("grantpt");
    close(master);
    return(-1);
  }

  /*
  //  unlock the slave pty 
  */
  if (unlockpt(master) < 0) {
    perror("unlockpt");
    close(master);
    return(-1);
  }

  /*
  //  start a child process 
  */
  if ((pid = fork()) < 0) {
    perror("fork in forkpty");
    close(master);
    return(-1);
  }

  /*
  //  the child process will open the slave, which will become
  //  its controlling terminal 
  */
  if (pid == 0) {
    /*
    //  get rid of our current controlling terminal 
    */
    setsid();

    /*
    //  get the name of the slave pseudo tty 
    */
    if ((slavename = ptsname(master)) == NULL) {
      perror("ptsname");
      close(master);
      return(-1);
    }

    /* 
    //  open the slave pseudo tty 
    */
    if ((slave = open(slavename, O_RDWR)) < 0) {
      perror(slavename);
      close(master);
      return(-1);
    }

#ifndef HAVE_AIX_PTY
    /*
    //  push the pseudo-terminal emulation module 
    */
    if (ioctl(slave, I_PUSH, "ptem") < 0) {
      perror("ioctl: ptem");
      close(master);
      close(slave);
      return(-1);
    }

    /*
    //  push the terminal line discipline module 
    */
    if (ioctl(slave, I_PUSH, "ldterm") < 0) {
      perror("ioctl: ldterm");
      close(master);
      close(slave);
      return(-1);
    }

#ifdef HAVE_TTCOMPAT
    /*
    //  push the compatibility for older ioctl calls module (solaris) 
    */
    if (ioctl(slave, I_PUSH, "ttcompat") < 0) {
      perror("ioctl: ttcompat");
      close(master);
      close(slave);
      return(-1);
    }
#endif
#endif

    /*
    //  copy the caller's terminal modes to the slave pty 
    */
    if (tcsetattr(slave, TCSANOW, termp) < 0) {
      perror("tcsetattr: slave pty");
      close(master);
      close(slave);
      return(-1);
    }

    /*
    //  set the slave pty window size to the caller's size 
    */
    if (ioctl(slave, TIOCSWINSZ, winp) < 0) {
      perror("ioctl: slave winsz");
      close(master);
      close(slave);
      return(-1);
    }

    /*
    //  close the logfile and the master pty
    //  no need for these in the slave process 
    */
    close(master);
#ifdef LOGTOSYSLOG
    closelog();
#endif
    fclose(logFile);
    /*
    //  set the slave to be our standard input, output,
    //  and error output.  Then get rid of the original
    //  file descriptor 
    */
    dup2(slave, 0);
    dup2(slave, 1);
    dup2(slave, 2);
    close(slave);
    /*
    //  if the caller wants it, give him back the slave pty's name 
    */
    if (name != NULL) strcpy(name, slavename);
    return(0); 
  } else {
    /*
    //  return the slave pty device name if caller wishes so 
    */
    if (name != NULL) {          
      if ((slavename = ptsname(master)) == NULL) {
        perror("ptsname");
        close(master);
        return(-1);
      }
      strcpy(name, slavename);
    }
    /*
    //  return the file descriptor for communicating with the process
    //  to our caller 
    */
    *amaster = master; 
    return(pid);      
  }
}
#endif

void version() {
  printf("%s version %s\n", basename(progName),VERSION);
#ifdef LOGTOFILE
  printf("logfiles go to directory %s\n", LOGDIR);
#endif
#ifdef LOGTOSYSLOG
  printf("syslog messages go to priority %s.%s\n", SYSLOGFACILITYNAME, SYSLOGPRIORITYNAME);
#else
  printf("no syslog logging\n");
#endif
#ifdef LINECNT
  printf("line numbering is on\n");
#else
  printf("line numbering is off\n");
#endif
#ifndef SUCMD
  printf("running as non-root user is not possible\n");
#endif
  exit(0);
}


void usage() {
  printf("Usage: %s [OPTION [ARG]] ...\n"
    " -?, --help            show this help statement\n"
    " -i, --login           start a (initial) login shell\n"
    " -u, --user username   run shell as a different user\n"
    " -V, --version         show version statement\n", basename(progName));
  exit(0);
}
