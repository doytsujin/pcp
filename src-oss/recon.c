/*
 * Copyright (c) 1997-2001 Silicon Graphics, Inc.  All Rights Reserved.
 */

#ident "$Id: recon.c,v 1.1 2002/10/25 01:33:55 kenmcd Exp $"

/*
 * ping pmcd ... when the connection is lost, note time.
 * keep pinging ... when connection is resumed, report time delta.
 *
 * Has to be run as root to control pmcd
 */

#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <pcp/pmapi.h>
#include <pcp/impl.h>

int
main(int argc, char **argv)
{
    struct timeval	now;
    struct timeval	then;
    pmResult	*rp;
    int		i;
    char	*namelist[4];
    pmID	pmidlist[4];
    int		numpmid;
    int		ctx;
    int		c;
    int		sts;
    char	*p;
    int		errflag = 0;
    extern char	*optarg;
    extern int	optind;
    extern int	pmDebug;

    /* trim command name of leading directory components */
    pmProgname = argv[0];
    for (p = pmProgname; *p; p++) {
	if (*p == '/')
	    pmProgname = p+1;
    }

    while ((c = getopt(argc, argv, "D:")) != EOF) {
	switch (c) {

#ifdef PCP_DEBUG

	case 'D':	/* debug flag */
	    sts = __pmParseDebug(optarg);
	    if (sts < 0) {
		fprintf(stderr, "%s: unrecognized debug flag specification (%s)\n",
		    pmProgname, optarg);
		errflag++;
	    }
	    else
		pmDebug |= sts;
	    break;
#endif

	case '?':
	default:
	    errflag++;
	    break;
	}
    }

    if (errflag) {
	fprintf(stderr,
"Usage: %s options ...\n\
\n\
Options:\n\
  -D N		set pmDebug debugging flag to N\n",
		pmProgname);
	exit(1);
    }

    if ((sts = pmLoadNameSpace(PM_NS_DEFAULT)) < 0) {
	fprintf(stderr, "pmLoadNameSpace: %s\n", pmErrStr(sts));
	exit(1);
    }

    if ((ctx = pmNewContext(PM_CONTEXT_HOST, "localhost")) < 0) {
	fprintf(stderr, "pmNewContext: %s\n", pmErrStr(ctx));
	exit(1);
    }

    i = 0;
    namelist[i++] = "sample.long.write_me";
    numpmid = i;
    sts = pmLookupName(numpmid, namelist, pmidlist);
    if (sts < 0) {
	fprintf(stderr, "pmLookupName: %s\n", pmErrStr(sts));
	for (i = 0; i < numpmid; i++) {
	    if (pmidlist[i] == PM_ID_NULL)
		fprintf(stderr, "	%s - not known\n", namelist[i]);
	}
	exit(1);
    }

    for ( ; ; ) {
	if ((sts = pmFetch(1, pmidlist, &rp)) < 0) {
	    fprintf(stderr, "pmFetch failed: %s\n", pmErrStr(sts));
	    if (sts != PM_ERR_IPC && sts != -ECONNRESET) {
		/* unexpected */
		fprintf(stderr, "Bogus error?\n");
		exit(1);
	    }
	    gettimeofday(&then, (struct timezone *)0);
	    break;
	}
	pmFreeResult(rp);
	sginap(10);
    }

    for ( ; ; ) {
	if ((sts = pmReconnectContext(ctx)) >= 0) {
	    fprintf(stderr, "pmReconnectContext: success\n");
	    gettimeofday(&now, (struct timezone *)0);
	    fprintf(stderr, "delay: %.0f secs\n", __pmtimevalSub(&now, &then));
	    break;
	}
	sginap(10);
    }

    exit(0);
    /*NOTREACHED*/
}
