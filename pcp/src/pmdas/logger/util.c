/*
 * Utiltiy functions for the logger PMDA.
 *
 * Copyright (c) 2011 Red Hat Inc.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <pcp/pmapi.h>
#include <pcp/impl.h>
#include "util.h"

void
rstrip(char *str)
{
    char *ptr;

    /* Remove all trailing whitespace.  Set ptr to last char of
     * string. */
    ptr = str + strlen(str) - 1;
    /* While trailing whitespace, move back. */
    while (ptr >= str && isspace(*ptr)) {
	--ptr;
    }
    *(ptr+1) = '\0';	  /* Now set '\0' as terminal byte. */
}

char *
lstrip(char *str)
{
    /* While leading whitespace, move forward. */
    char *ptr = str;
    while (*ptr != '\0' && isspace(*ptr)) {
	ptr++;
    }
    return ptr;
}

int
start_cmd(const char *cmd, pid_t *ppid)
{
    pid_t child_pid;
    int rc;
    int pipe_fds[2];
#define PARENT_END 0			/* parent end of the pipe */
#define CHILD_END 1			/* child end of the pipe */
#define STDOUT_FD 1			/* stdout fd */

    /* FIXME items:
     * (1) Should we be looking to handle shell metachars
     * differently?  Perhaps we should just allow isalnum()||isspace()
     * chars only.
     * (2) Do we need to clean up the environment in the child before
     * the exec()?  Remove things like IFS, CDPATH, ENV, and BASH_ENV.
     */

    __pmNotifyErr(LOG_INFO, "%s: Trying to run command: %s", __FUNCTION__,
		  cmd);

    /* Create the pipes. */
    rc = pipe2(pipe_fds, O_CLOEXEC|O_NONBLOCK);
    if (rc < 0) {
	__pmNotifyErr(LOG_ERR, "%s: pipe2() returned %s", __FUNCTION__,
		      strerror(-rc));
	return rc;
    }

    /* Create the new process. */
    child_pid = fork();
    if (child_pid == 0) {		/* child process */
	int i;

	/* Duplicate our pipe fd onto stdout of the child. Note that
	 * this clears O_CLOEXEC, so the new stdout should stay open
	 * when we call exec(). */
	if (dup2(pipe_fds[CHILD_END], STDOUT_FD) < 0) {
	    __pmNotifyErr(LOG_ERR, "%s: dup2() returned %s", __FUNCTION__,
		      strerror(errno));
	    _exit(127);
	}

	/* Close all other fds. */
	for (i = 0; i <= pipe_fds[CHILD_END]; i++) {
	    if (i != STDOUT_FD) {
		close(i);
	    }
	}

	/* Actually run the command. */
	execl ("/bin/sh", "sh", "-c", cmd, (char *)NULL);
	_exit (127);

    }
    else if (child_pid > 0) {		/* parent process */
	close (pipe_fds[PARENT_END]);
	if (ppid != NULL) {
	    *ppid = child_pid;
	}
    }
    else if (child_pid < 0) {		/* fork error */
	int errno_save = errno;

	__pmNotifyErr(LOG_ERR, "%s: fork() returned %s", __FUNCTION__,
		      strerror(errno_save));
	close (pipe_fds[PARENT_END]);
	close (pipe_fds[CHILD_END]);

	return -errno_save;
    }
    
    return pipe_fds[CHILD_END];
}

int
stop_cmd(pid_t pid)
{
    int rc;
    pid_t wait_pid;
    int wstatus;

    __pmNotifyErr(LOG_INFO, "%s: killing pid %d", __FUNCTION__, (int)pid);

    /* Send the TERM signal. */
    rc = kill(pid, SIGTERM);
    __pmNotifyErr(LOG_INFO, "%s: kill returned %d", __FUNCTION__, rc);

    /* Wait for the process to go away. */
    do {
	wait_pid = waitpid (pid, &wstatus, 0);
	__pmNotifyErr(LOG_INFO, "%s: waitpid returned %d", __FUNCTION__,
		      (int)wait_pid);
    } while (wait_pid == -1 && errno == EINTR);

    /* Return process exit status. */
    return wstatus;
}
