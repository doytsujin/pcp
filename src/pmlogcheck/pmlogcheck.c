/*
 * Copyright (c) 1995-2002 Silicon Graphics, Inc.
 * Copyright (c) 2013 Ken McDonell.
 * All Rights Reserved.
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
 */

#include "pmapi.h"
#include "impl.h"
#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>

#define STS_FATAL	-2
#define STS_WARNING	-1
#define STS_OK		0

static struct timeval	tv;
static int		numpmid;
static pmID		*pmid;
static char		*archpathname;	/* from the command line */
static char		*archbasename;	/* after basename() */
static char		*archdirname;	/* after dirname() */
#define STATE_OK	1
#define STATE_MISSING	2
#define STATE_BAD	3
static int		index_state = STATE_MISSING;
static int		meta_state = STATE_MISSING;
static int		log_state = STATE_MISSING;
static int		sflag;
static int		vflag;
static int		sep;

/*
 * return -1, 0 or 1 as the struct timeval's compare
 * a < b, a == b or a > b
 */
int
tvcmp(struct timeval a, struct timeval b)
{
    if (a.tv_sec < b.tv_sec)
	return -1;
    if (a.tv_sec > b.tv_sec)
	return 1;
    if (a.tv_usec < b.tv_usec)
	return -1;
    if (a.tv_usec > b.tv_usec)
	return 1;
    return 0;
}

static int
do_size(pmResult *rp)
{
    int		nbyte = 0;
    int		i;
    int		j;
    /*
     *  Externally the log record looks like this ...
     *  :----------:-----------:..........:---------:
     *  | int len  | timestamp | pmResult | int len |
     *  :----------:-----------:..........:---------:
     *
     * start with sizes of the header len, timestamp, numpmid, and
     * trailer len
     */
    nbyte = sizeof(int) + sizeof(__pmTimeval) + sizeof(int);
    						/* len + timestamp + len */
    nbyte += sizeof(int);
    							/* numpmid */
    for (i = 0; i < rp->numpmid; i++) {
	pmValueSet	*vsp = rp->vset[i];
	nbyte += sizeof(pmID) + sizeof(int);		/* + pmid[i], numval */
	if (vsp->numval > 0) {
	    nbyte += sizeof(int);			/* + valfmt */
	    for (j = 0; j < vsp->numval; j++) {
		nbyte += sizeof(__pmValue_PDU);		/* + pmValue[j] */
		if (vsp->valfmt != PM_VAL_INSITU)
							/* + pmValueBlock */
							/* rounded up */
		    nbyte += PM_PDU_SIZE_BYTES(vsp->vlist[j].value.pval->vlen);
	    }
	}
    }

    return nbyte;
}

void
dumpresult(pmResult *resp)
{
    int		i;
    int		j;
    int		n;
    char	*mname;
    char	*iname;
    pmDesc	desc;

    if (sflag) {
	int		nbyte;
	nbyte = do_size(resp);
	printf("[%d bytes]\n", nbyte);
    }

    __pmPrintStamp(stdout, &resp->timestamp);

    if (resp->numpmid == 0) {
	printf("  <mark>\n");
	return;
    }

    for (i = 0; i < resp->numpmid; i++) {
	pmValueSet	*vsp = resp->vset[i];

	if (i > 0)
	    printf("            ");
	if ((n = pmNameID(vsp->pmid, &mname)) < 0)
	    mname = strdup("<noname>");
	if (vsp->numval == 0) {
	    printf("  %s (%s): No values returned!\n", pmIDStr(vsp->pmid), mname);
	    goto next;
	}
	else if (vsp->numval < 0) {
	    printf("  %s (%s): %s\n", pmIDStr(vsp->pmid), mname, pmErrStr(vsp->numval));
	    goto next;
	}

	if (pmLookupDesc(vsp->pmid, &desc) < 0) {
	    /* don't know, so punt on the most common cases */
	    desc.indom = PM_INDOM_NULL;
	    if (vsp->valfmt == PM_VAL_INSITU)
		desc.type = PM_TYPE_32;
	    else
		desc.type = PM_TYPE_AGGREGATE;
	}

	for (j = 0; j < vsp->numval; j++) {
	    pmValue	*vp = &vsp->vlist[j];
	    if (desc.type == PM_TYPE_EVENT) {
		/* largely lifted from pminfo -x ... */
		int		r;		/* event records */
		int		p;		/* event parameters */
		int		nrecords;
		int		nmissed = 0;
		int		flags;
		pmResult	**res;
		static pmID	pmid_flags = 0;
		static pmID	pmid_missed;

		if (j > 0)
		    printf("            ");
		printf("  %s (%s", pmIDStr(vsp->pmid), mname);
		if (desc.indom != PM_INDOM_NULL) {
		    putchar('[');
		    if (pmNameInDom(desc.indom, vp->inst, &iname) < 0)
			printf("%d or ???])", vp->inst);
		    else {
			printf("%d or \"%s\"])", vp->inst, iname);
			free(iname);
		    }
		}
		else {
		    printf(")");
		}
		printf(": ");

		nrecords = pmUnpackEventRecords(vsp, j, &res);
		if (nrecords < 0)
		    continue;
		if (nrecords == 0) {
		    printf("No event records\n");
		    continue;
		}

		if (pmid_flags == 0) {
		    /*
		     * get PMID for event.flags and event.missed
		     * note that pmUnpackEventRecords() will have called
		     * __pmRegisterAnon(), so the anon metrics
		     * should now be in the PMNS
		     */
		    char	*name_flags = "event.flags";
		    char	*name_missed = "event.missed";
		    int	sts;
		    sts = pmLookupName(1, &name_flags, &pmid_flags);
		    if (sts < 0) {
			/* should not happen! */
			fprintf(stderr, "Warning: cannot get PMID for %s: %s\n", name_flags, pmErrStr(sts));
			/* avoid subsequent warnings ... */
			__pmid_int(&pmid_flags)->item = 1;
		    }
		    sts = pmLookupName(1, &name_missed, &pmid_missed);
		    if (sts < 0) {
			/* should not happen! */
			fprintf(stderr, "Warning: cannot get PMID for %s: %s\n", name_missed, pmErrStr(sts));
			/* avoid subsequent warnings ... */
			__pmid_int(&pmid_missed)->item = 1;
		    }
		}

		for (r = 0; r < nrecords; r++) {
		    if (res[r]->numpmid == 2 && res[r]->vset[0]->pmid == pmid_flags &&
			(res[r]->vset[0]->vlist[0].value.lval & PM_EVENT_FLAG_MISSED) &&
			res[r]->vset[1]->pmid == pmid_missed) {
			nmissed += res[r]->vset[1]->vlist[0].value.lval;
		    }
		}

		printf("%d", nrecords);
		if (nmissed > 0)
		    printf(" (and %d missed)", nmissed);
		if (nrecords + nmissed == 1)
		    printf(" event record\n");
		else
		    printf(" event records\n");
		for (r = 0; r < nrecords; r++) {
		    printf("              --- event record [%d] timestamp ", r);
		    __pmPrintStamp(stdout, &res[r]->timestamp);
		    if (res[r]->numpmid == 0) {
			printf(" ---\n");
			printf("	          No parameters\n");
			continue;
		    }
		    if (res[r]->numpmid < 0) {
			printf(" ---\n");
			printf("	          Error: illegal number of parameters (%d)\n", res[r]->numpmid);
			continue;
		    }
		    flags = 0;
		    for (p = 0; p < res[r]->numpmid; p++) {
			pmValueSet	*xvsp = res[r]->vset[p];
			int		sts;
			pmDesc	desc;
			char	*name;

			if (pmNameID(xvsp->pmid, &name) >= 0) {
			    if (p == 0) {
				if (xvsp->pmid == pmid_flags) {
				    flags = xvsp->vlist[0].value.lval;
				    printf(" flags 0x%x", flags);
				    printf(" (%s) ---\n", pmEventFlagsStr(flags));
				    free(name);
				    continue;
				}
				printf(" ---\n");
			    }
			    if ((flags & PM_EVENT_FLAG_MISSED) && p == 1 && xvsp->pmid == pmid_missed) {
				printf("              ==> %d missed event records\n", xvsp->vlist[0].value.lval);
				free(name);
				continue;
			    }
			    printf("                %s (%s):", pmIDStr(xvsp->pmid), name);
			    free(name);
			}
			else
			    printf("	            PMID: %s:", pmIDStr(xvsp->pmid));
			if ((sts = pmLookupDesc(xvsp->pmid, &desc)) < 0) {
			    printf(" pmLookupDesc: %s\n", pmErrStr(sts));
			    continue;
			}
			printf(" value ");
			pmPrintValue(stdout, xvsp->valfmt, desc.type, &xvsp->vlist[0], 1);
			putchar('\n');
		    }
		}
		pmFreeEventResult(res);
	    }
	    else {
		if (j == 0) {
		    printf("  %s (%s):", pmIDStr(vsp->pmid), mname);
		    if (vsp->numval > 1) {
			putchar('\n');
			printf("               ");
		    }
		}
		else
		    printf("               ");
		if (desc.indom != PM_INDOM_NULL) {
		    printf(" inst [");
		    if (pmNameInDom(desc.indom, vp->inst, &iname) < 0)
			printf("%d or ???]", vp->inst);
		    else {
			printf("%d or \"%s\"]", vp->inst, iname);
			free(iname);
		    }
		}
		printf(" value ");
		pmPrintValue(stdout, vsp->valfmt, desc.type, vp, 1);
		putchar('\n');
	    }
	}
next:
	if (mname)
	    free(mname);
    }
}

static void
dumpDesc(__pmContext *ctxp)
{
    int		i;
    int		sts;
    char	*p;
    __pmHashNode	*hp;
    pmDesc	*dp;

    printf("\nDescriptions for Metrics in the Log ...\n");
    for (i = 0; i < ctxp->c_archctl->ac_log->l_hashpmid.hsize; i++) {
	for (hp = ctxp->c_archctl->ac_log->l_hashpmid.hash[i]; hp != NULL; hp = hp->next) {
	    dp = (pmDesc *)hp->data;
	    sts = pmNameID(dp->pmid, &p);
	    if (sts < 0)
		printf("PMID: %s (%s)\n", pmIDStr(dp->pmid), "<noname>");
	    else {
		printf("PMID: %s (%s)\n", pmIDStr(dp->pmid), p);
		free(p);
	    }
	    __pmPrintDesc(stdout, dp);
	}
    }
}

static void
dumpInDom(__pmContext *ctxp)
{
    int		i;
    int		j;
    __pmHashNode	*hp;
    __pmLogInDom	*idp;
    __pmLogInDom	*ldp;

    printf("\nInstance Domains in the Log ...\n");
    for (i = 0; i < ctxp->c_archctl->ac_log->l_hashindom.hsize; i++) {
	for (hp = ctxp->c_archctl->ac_log->l_hashindom.hash[i]; hp != NULL; hp = hp->next) {
	    printf("InDom: %s\n", pmInDomStr((pmInDom)hp->key));
	    /*
	     * in reverse chronological order, so iteration is a bit funny
	     */
	    ldp = NULL;
	    for ( ; ; ) {
		for (idp = (__pmLogInDom *)hp->data; idp->next != ldp; idp =idp->next)
			;
		tv.tv_sec = idp->stamp.tv_sec;
		tv.tv_usec = idp->stamp.tv_usec;
		__pmPrintStamp(stdout, &tv);
		printf(" %d instances\n", idp->numinst);
		for (j = 0; j < idp->numinst; j++) {
		    printf("                 %d or \"%s\"\n",
			idp->instlist[j], idp->namelist[j]);
		}
		if (idp == (__pmLogInDom *)hp->data)
		    break;
		ldp = idp;
	    }
	}
    }
}

/*
 * check the temporal index
 */
static void
pass1(__pmContext *ctxp)
{
    int		i;
    char	path[MAXPATHLEN];
    off_t	meta_size = -1;		/* initialize to pander to gcc */
    off_t	log_size = -1;		/* initialize to pander to gcc */
    struct stat	sbuf;
    __pmLogTI	*tip;
    __pmLogTI	*lastp;

    if (vflag) printf("pass1: check temporal index\n");
    lastp = NULL;
    for (i = 1; i <= ctxp->c_archctl->ac_log->l_numti; i++) {
	/*
	 * Integrity Checks
	 *
	 * this(tv_sec) < 0
	 * this(tv_usec) < 0 || this(tv_usec) > 999999
	 * this(timestamp) < last(timestamp)
	 * this(vol) >= 0
	 * this(vol) < last(vol)
	 * this(vol) == last(vol) && this(meta) <= last(meta)
	 * this(vol) == last(vol) && this(log) <= last(log)
	 * file_exists(<base>.meta) && this(meta) > file_size(<base>.meta)
	 * file_exists(<base>.this(vol)) &&
	 *		this(log) > file_size(<base>.this(vol))
	 *
	 * Integrity Warnings
	 *
	 * this(vol) != last(vol) && !file_exists(<base>.this(vol))
	 */
	tip = &ctxp->c_archctl->ac_log->l_ti[i-1];
	tv.tv_sec = tip->ti_stamp.tv_sec;
	tv.tv_usec = tip->ti_stamp.tv_usec;
	if (i == 1) {
	    sprintf(path, "%s%c%s.meta", archdirname, sep, archbasename);
	    if (stat(path, &sbuf) == 0)
		meta_size = sbuf.st_size;
	    else {
		/* should not get here ... as detected in after pass0 */
		fprintf(stderr, "%s: pass1: Botch: cannot open metadata file (%s)\n", pmProgname, path);
		exit(1);
	    }
	}
	if (tip->ti_vol < 0) {
	    printf("%s.index[entry %d]: illegal volume number %d\n",
		    archbasename, i, tip->ti_vol);
	    index_state = STATE_BAD;
	    log_size = -1;
	}
	else if (lastp == NULL || tip->ti_vol != lastp->ti_vol) { 
	    sprintf(path, "%s%c%s.%d", archdirname, sep, archbasename, tip->ti_vol);
	    if (stat(path, &sbuf) == 0)
		log_size = sbuf.st_size;
	    else {
		log_size = -1;
		printf("%s: File missing or compressed for log volume %d\n", path, tip->ti_vol);
	    }
	}
	if (tip->ti_stamp.tv_sec < 0 ||
	    tip->ti_stamp.tv_usec < 0 || tip->ti_stamp.tv_usec > 999999) {
	    printf("%s.index[entry %d]: Illegal timestamp value (%d sec, %d usec)\n",
		archbasename, i, tip->ti_stamp.tv_sec, tip->ti_stamp.tv_usec);
	    index_state = STATE_BAD;
	}
	if (tip->ti_meta < sizeof(__pmLogLabel)+2*sizeof(int)) {
	    printf("%s.index[entry %d]: offset to metadata (%ld) before end of label record (%ld)\n",
		archbasename, i, (long)tip->ti_meta, (long)(sizeof(__pmLogLabel)+2*sizeof(int)));
	    index_state = STATE_BAD;
	}
	if (meta_size != -1 && tip->ti_meta > meta_size) {
	    printf("%s.index[entry %d]: offset to metadata (%ld) past end of file (%ld)\n",
		archbasename, i, (long)tip->ti_meta, (long)meta_size);
	    index_state = STATE_BAD;
	}
	if (tip->ti_log < sizeof(__pmLogLabel)+2*sizeof(int)) {
	    printf("%s.index[entry %d]: offset to log (%ld) before end of label record (%ld)\n",
		archbasename, i, (long)tip->ti_log, (long)(sizeof(__pmLogLabel)+2*sizeof(int)));
	    index_state = STATE_BAD;
	}
	if (log_size != -1 && tip->ti_log > log_size) {
	    printf("%s.index[entry %d]: offset to log (%ld) past end of file (%ld)\n",
		archbasename, i, (long)tip->ti_log, (long)log_size);
	    index_state = STATE_BAD;
	}
	if (lastp != NULL) {
	    if (tip->ti_stamp.tv_sec < lastp->ti_stamp.tv_sec ||
	        (tip->ti_stamp.tv_sec == lastp->ti_stamp.tv_sec &&
	         tip->ti_stamp.tv_usec < lastp->ti_stamp.tv_usec)) {
		printf("%s.index: timestamp went backwards in time %d.%06d[entry %d]-> %d.%06d[entry %d]\n",
			archbasename, (int)lastp->ti_stamp.tv_sec, (int)lastp->ti_stamp.tv_usec, i-1,
			(int)tip->ti_stamp.tv_sec, (int)tip->ti_stamp.tv_usec, i);
		index_state = STATE_BAD;
	    }
	    if (tip->ti_vol < lastp->ti_vol) {
		printf("%s.index: volume number decreased %d[entry %d] -> %d[entry %d]\n",
			archbasename, lastp->ti_vol, i-1, tip->ti_vol, i);
		index_state = STATE_BAD;
	    }
	    if (tip->ti_vol == lastp->ti_vol && tip->ti_meta < lastp->ti_meta) {
		printf("%s.index: offset to metadata decreased %ld[entry %d] -> %ld[entry %d]\n",
			archbasename, (long)lastp->ti_meta, i-1, (long)tip->ti_meta, i);
		index_state = STATE_BAD;
	    }
	    if (tip->ti_vol == lastp->ti_vol && tip->ti_log < lastp->ti_log) {
		printf("%s.index: offset to log decreased %ld[entry %d] -> %ld[entry %d]\n",
			archbasename, (long)lastp->ti_log, i-1, (long)tip->ti_log, i);
		index_state = STATE_BAD;
	    }
	}
	lastp = tip;
    }
}

static int
filter(const_dirent *dp)
{
    static int	len = -1;

    if (len == -1)
	len = strlen(archbasename);
    if (strncmp(dp->d_name, archbasename, len) != 0)
	return 0;
    if (strcmp(&dp->d_name[len], ".meta") == 0)
	return 1;
    if (strcmp(&dp->d_name[len], ".index") == 0)
	return 1;
    if (dp->d_name[len] == '.' && isdigit(dp->d_name[len+1])) {
	const char	*p = &dp->d_name[len+2];
	for ( ; *p; p++) {
	    if (!isdigit(*p))
		return 0;
	}
	return 1;
    }
    return 0;
}

#define IS_UNKNOWN	0
#define IS_INDEX	1
#define IS_META		2
#define IS_LOG		3
/*
 * Pass 1 for all files
 * - should only come here if fname exists
 * - check that file contains a number of complete records
 * - the label record and the records for the data and metadata
 *   have this format:
 *   :----------:----------------------:---------:
 *   | int len  |        stuff         | int len |
 *   | header   |        stuff         | trailer |
 *   :----------:----------------------:---------:
 *   and the len fields are in network byte order.
 *   For these records, check that the header length is equal to
 *   the trailer length
 * - for index files, following the label record there should be
 *   a number of complete records, each of which is a __pmLogTI
 *   record, with the fields converted network byte order
 *
 * TODO - repair
 * - truncate metadata and data files ... unconditional or interactive confirm?
 * - mark index as bad and needing rebuild
 * - move access check into here (if cannot open file for reading we're screwed)
 */
static int
pass0(char *fname)
{
    int		len;
    int		check;
    int		i;
    int		sts;
    int		nrec = 0;
    int		is = IS_UNKNOWN;
    char	*p;
    FILE	*f;

    if (vflag)
	printf("pass0: %s:\n", fname);
    if ((f = fopen(fname, "r")) == NULL) {
	fprintf(stderr, "%s: Cannot open \"%s\": %s\n", pmProgname, fname, osstrerror());
	sts = STS_FATAL;
	goto done;
    }
    p = strrchr(fname, '.');
    if (p != NULL) {
	if (strcmp(p, ".index") == 0)
	    is = IS_INDEX;
	else if (strcmp(p, ".meta") == 0)
	    is = IS_META;
	else if (isdigit(*++p)) {
	    p++;
	    while (*p && isdigit(*p))
		p++;
	    if (*p == '\0')
		is = IS_LOG;
	}
    }
    if (is == IS_UNKNOWN) {
	/*
	 * should never get here because filter() is supposed to
	 * only include PCP archive file names from scandir()
	 */
	fprintf(stderr, "%s: pass0: Botch: bad file name? %s\n", pmProgname, fname);
	exit(1);
    }

    while ((sts = fread(&len, 1, sizeof(len), f)) == sizeof(len)) {
	len = ntohl(len);
	len -= 2 * sizeof(len);
	/* gobble stuff between header and trailer without looking at it */
	for (i = 0; i < len; i++) {
	    check = fgetc(f);
	    if (check == EOF) {
		if (nrec == 0)
		    printf("%s: unexpected EOF in label record body\n", fname);
		else
		    printf("%s[record %d]: unexpected EOF in record body\n", fname, nrec);
		sts = STS_FATAL;
		goto done;
	    }
	}
	if ((sts = fread(&check, 1, sizeof(check), f)) != sizeof(check)) {
	    if (nrec == 0)
		printf("%s: unexpected EOF in label record trailer\n", fname);
	    else
		printf("%s[record %d]: unexpected EOF in record trailer\n", fname, nrec);
	    sts = STS_FATAL;
	    goto done;
	}
	check = ntohl(check);
	len += 2 * sizeof(len);
	if (check != len) {
	    if (nrec == 0)
		printf("%s: label record length mismatch: header %d != trailer %d\n", fname, len, check);
	    else
		printf("%s[record %d]: length mismatch: header %d != trailer %d\n", fname, nrec, len, check);
	    sts = STS_FATAL;
	    goto done;
	}
	nrec++;
	if (is == IS_INDEX) {
	    /* for index files, done label record, now eat index records */
	    __pmLogTI	tirec;
	    while ((sts = fread(&tirec, 1, sizeof(tirec), f)) == sizeof(tirec)) { 
		nrec++;
	    }
	    if (sts != 0) {
		printf("%s[record %d]: unexpected EOF in index entry\n", fname, nrec);
		index_state = STATE_BAD;
		sts = STS_FATAL;
		goto done;
	    }
	    goto empty_check;
	}
    }
    if (sts != 0) {
	printf("%s[record %d]: unexpected EOF in record header\n", fname, nrec);
	sts = STS_FATAL;
    }
empty_check:
    if (sts != STS_FATAL && nrec < 2) {
	printf("%s: contains no PCP data\n", fname);
	sts = STS_WARNING;
    }
    /*
     * sts == 0 (from fread) => STS_OK
     */
done:
    if (is == IS_INDEX) {
	if (sts == STS_OK)
	    index_state = STATE_OK;
	else
	    index_state = STATE_BAD;
    }
    else if (is == IS_META) {
	if (sts == STS_OK)
	    meta_state = STATE_OK;
	else
	    meta_state = STATE_BAD;
    }
    else {
	if (log_state == STATE_OK && sts != STS_OK)
	    log_state = STATE_BAD;
	else if (log_state == STATE_MISSING) {
	    if (sts == STS_OK)
		log_state = STATE_OK;
	    else
		log_state = STATE_BAD;
	}
    }

    return sts;
}

static void
dometric(const char *name)
{
    int		sts;

    if (*name == '\0') {
	printf("PMNS appears to be empty!\n");
	return;
    }
    numpmid++;
    pmid = (pmID *)realloc(pmid, numpmid * sizeof(pmID));
    if ((sts = pmLookupName(1, (char **)&name, &pmid[numpmid-1])) < 0) {
	fprintf(stderr, "%s: pmLookupName(%s): %s\n", pmProgname, name, pmErrStr(sts));
	numpmid--;
    }
}

int
main(int argc, char *argv[])
{
    int			c;
    int			sts;
    int			errflag = 0;
    int			i;
    int			n;
    int			dflag = 0;
    int			iflag = 0;
    int			Lflag = 0;
    int			lflag = 0;
    int			mflag = 0;
    int			tflag = 0;
    int			mode = PM_MODE_FORW;
    char		*p;
    __pmContext		*ctxp;
    pmResult		*result;
    struct timeval 	endTime = { INT_MAX, 0 };
    struct timeval	appStart;
    struct timeval	appEnd;
    pmLogLabel		label;			/* get hostname for archives */
    int			zflag = 0;		/* for -z */
    char 		*tz = NULL;		/* for -Z timezone */
    char		timebuf[32];		/* for pmCtime result + .xxx */
    struct timeval	done;
    struct dirent	**namelist;
    int			nfile;

    __pmSetProgname(argv[0]);
    sep = __pmPathSeparator();
    setlinebuf(stdout);
    setlinebuf(stderr);

    while ((c = getopt(argc, argv, "aD:dilLmst:vZ:z?")) != EOF) {
	switch (c) {

	case 'a':	/* dump everything */
	    dflag = iflag = lflag = mflag = sflag = tflag = 1;
	    break;

	case 'd':	/* dump pmDesc structures */
	    dflag = 1;
	    break;

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

	case 'i':	/* dump instance domains */
	    iflag = 1;
	    break;

	case 'L':	/* dump label, verbose */
	    Lflag = 1;
	    lflag = 1;
	    break;

	case 'l':	/* dump label */
	    lflag = 1;
	    break;

	case 'm':	/* dump metrics in log */
	    mflag = 1;
	    break;

	case 's':	/* report data size in log */
	    sflag = 1;
	    break;

	case 'v':	/* verbose */
	    vflag = 1;
	    break;

	case 'z':	/* timezone from host */
	    if (tz != NULL) {
		fprintf(stderr, "%s: at most one of -Z and/or -z allowed\n", pmProgname);
		errflag++;
	    }
	    zflag++;
	    break;

	case 'Z':	/* $TZ timezone */
	    if (zflag) {
		fprintf(stderr, "%s: at most one of -Z and/or -z allowed\n", pmProgname);
		errflag++;
	    }
	    tz = optarg;
	    break;

	case '?':
	default:
	    errflag++;
	    break;
	}
    }

    if (errflag || optind > argc-1) {
	fprintf(stderr,
"Usage: %s [options] archive [metricname ...]\n"
"\n"
"Options:\n"
"  -a            dump everything\n"
"  -d            dump metric descriptions\n"
"  -i            dump instance domain descriptions\n"
"  -L            more verbose form of -l\n"
"  -l            dump the archive label\n"
"  -m            dump values of the metrics (default)\n"
"  -r            process archive in reverse chronological order\n"
"  -s            report size of data records in archive\n"
"  -t            dump the temporal index\n"
"  -v            verbose output\n"
"  -Z timezone   set reporting timezone\n"
"  -z            set reporting timezone to local time for host from -a\n",
                pmProgname);
	exit(1);
    }

    archpathname = argv[optind];
    archbasename = strdup(basename(strdup(archpathname)));
    /*
     * treat foo, foo.index, foo.meta, foo.NNN as all equivalent
     * to "foo"
     */
    p = strrchr(archbasename, '.');
    if (p != NULL) {
	if (strcmp(p, ".index") == 0 || strcmp(p, ".meta") == 0)
	    *p = '\0';
	else {
	    char	*q = ++p;
	    if (isdigit(*q)) {
		q++;
		while (*q && isdigit(*q))
		    q++;
		if (*q == '\0')
		    *p = '\0';
	    }
	}
    }
    archdirname = dirname(strdup(archpathname));
    if (vflag)
	printf("Scanning for components of archive \"%s\"\n", archpathname);
    nfile = scandir(archdirname, &namelist, filter, NULL);
    if (nfile < 1) {
	fprintf(stderr, "%s: No PCP archive files match \"%s\"\n", pmProgname, archpathname);
	exit(1);
    }
    /*
     * Pass 0 for data and metadata files
     */
    sts = STS_OK;
    for (i = 0; i < nfile; i++) {
	char	path[MAXPATHLEN];
	snprintf(path, sizeof(path), "%s%c%s", archdirname, sep, namelist[i]->d_name);
	if (pass0(path) == STS_FATAL)
	    /* unrepairable or unrepaired error */
	    sts = STS_FATAL;
    }
    if (meta_state == STATE_MISSING) {
	fprintf(stderr, "%s: Missing metadata file (%s%c%s.meta)\n", pmProgname, archdirname, sep, archbasename);
	sts = STS_FATAL;
    }
    if (log_state == STATE_MISSING) {
	fprintf(stderr, "%s: Missing log file (%s%c%s.0 or similar)\n", pmProgname, archdirname, sep, archbasename);
	sts = STS_FATAL;
    }

    if (sts == STS_FATAL) {
	if (vflag) printf("Cannot continue ... bye\n");
	exit(1);
    }

    if ((sts = pmNewContext(PM_CONTEXT_ARCHIVE, archpathname)) < 0) {
	fprintf(stderr, "%s: Cannot open archive \"%s\": %s\n", pmProgname, archpathname, pmErrStr(sts));
	fprintf(stderr, "Checking abandoned.\n");
	exit(1);
    }
    if ((n = pmWhichContext()) >= 0) {
	if ((ctxp = __pmHandleToPtr(n)) == NULL) {
	    fprintf(stderr, "%s: Botch: __pmHandleToPtr(%d) returns NULL!\n", pmProgname, n);
	    exit(1);
	}
    }
    else {
	fprintf(stderr, "%s: Botch: %s!\n", pmProgname, pmErrStr(PM_ERR_NOCONTEXT));
	exit(1);
    }
    /*
     * Note: ctxp->c_lock remains locked throughout ... __pmHandleToPtr()
     *       is only called once, and a single context is used throughout
     *       ... so there is no PM_UNLOCK(ctxp->c_lock) anywhere in the
     *       pmchecklog code.
     *       This works because ctxp->c_lock is a recursive lock and
     *       pmchecklog is single-threaded.
     */

    pass1(ctxp);
    exit(1);

    numpmid = argc - optind;
    if (numpmid) {
	numpmid = 0;
	pmid = NULL;
	for (i = 0 ; optind < argc; i++, optind++) {
	    numpmid++;
	    pmid = (pmID *)realloc(pmid, numpmid * sizeof(pmID));
	    if ((sts = pmLookupName(1, &argv[optind], &pmid[numpmid-1])) < 0) {
		if (sts == PM_ERR_NONLEAF) {
		    numpmid--;
		    if ((sts = pmTraversePMNS(argv[optind], dometric)) < 0)
			fprintf(stderr, "%s: pmTraversePMNS(%s): %s\n", pmProgname, argv[optind], pmErrStr(sts));
		}
		else
		    fprintf(stderr, "%s: pmLookupName(%s): %s\n", pmProgname, argv[optind], pmErrStr(sts));
		if (sts < 0)
		    numpmid--;
	    }
	}
	if (numpmid == 0) {
	    fprintf(stderr, "No metric names can be translated, dump abandoned\n");
	    exit(1);
	}
    }

    if ((sts = pmGetArchiveLabel(&label)) < 0) {
	fprintf(stderr, "%s: Cannot get archive label record: %s\n",
	    pmProgname, pmErrStr(sts));
	exit(1);
    }

    if ((sts = pmGetArchiveEnd(&endTime)) < 0) {
	endTime.tv_sec = INT_MAX;
	endTime.tv_usec = 0;
	fflush(stdout);
	fprintf(stderr, "%s: Cannot locate end of archive: %s\n",
	    pmProgname, pmErrStr(sts));
	fprintf(stderr, "\nWARNING: This archive is sufficiently damaged that it may not be possible to\n");
	fprintf(stderr, "         produce complete information.  Continuing and hoping for the best.\n\n");
	fflush(stderr);
    }

    if (zflag) {
	if ((sts = pmNewContextZone()) < 0) {
	    fprintf(stderr, "%s: Cannot set context timezone: %s\n",
		pmProgname, pmErrStr(sts));
	    exit(1);
	}
	printf("Note: timezone set to local timezone of host \"%s\" from archive\n\n",
	    label.ll_hostname);
    }
    else if (tz != NULL) {
	if ((sts = pmNewZone(tz)) < 0) {
	    fprintf(stderr, "%s: Cannot set timezone to \"%s\": %s\n",
		pmProgname, tz, pmErrStr(sts));
	    exit(1);
	}
	printf("Note: timezone set to \"TZ=%s\"\n\n", tz);
    }

    if (sts < 0) {
	fprintf(stderr, "%s:\n%s\n", pmProgname, p);
	exit(1);
    }

    if (lflag) {
	char	       *ddmm;
	char	       *yr;

	printf("Log Label (Log Format Version %d)\n", label.ll_magic & 0xff);
	printf("Performance metrics from host %s\n", label.ll_hostname);

	ddmm = pmCtime(&label.ll_start.tv_sec, timebuf);
	ddmm[10] = '\0';
	yr = &ddmm[20];
	printf("  commencing %s ", ddmm);
	__pmPrintStamp(stdout, &label.ll_start);
	printf(" %4.4s\n", yr);

	if (endTime.tv_sec == INT_MAX) {
	    /* pmGetArchiveEnd() failed! */
	    printf("  ending     UNKNOWN\n");
	}
	else {
	    ddmm = pmCtime(&endTime.tv_sec, timebuf);
	    ddmm[10] = '\0';
	    yr = &ddmm[20];
	    printf("  ending     %s ", ddmm);
	    __pmPrintStamp(stdout, &endTime);
	    printf(" %4.4s\n", yr);
	}

	if (Lflag) {
	    printf("Archive timezone: %s\n", label.ll_tz);
	    printf("PID for pmlogger: %" FMT_PID "\n", label.ll_pid);
	}
    }

    if (dflag)
	dumpDesc(ctxp);

    if (iflag)
	dumpInDom(ctxp);


    if (mflag) {
	if (mode == PM_MODE_FORW) {
	    sts = pmSetMode(mode, &appStart, 0);
	    done = appEnd;
	}
	else {
	    appEnd.tv_sec = INT_MAX;
	    sts = pmSetMode(mode, &appEnd, 0);
	    done = appStart;
	}
	if (sts < 0) {
	    fprintf(stderr, "%s: pmSetMode: %s\n", pmProgname, pmErrStr(sts));
	    exit(1);
	}
	sts = 0;
	for ( ; ; ) {
	    sts = pmFetchArchive(&result);
	    if (sts < 0)
		break;
	    if ((mode == PM_MODE_FORW && tvcmp(result->timestamp, done) > 0) ||
		(mode == PM_MODE_BACK && tvcmp(result->timestamp, done) < 0)) {
		sts = PM_ERR_EOL;
		break;
	    }
	    putchar('\n');
	    dumpresult(result);
	    pmFreeResult(result);
	}
	if (sts != PM_ERR_EOL) {
	    fprintf(stderr, "%s: pmFetch: %s\n", pmProgname, pmErrStr(sts));
	    exit(1);
	}
    }

    exit(0);
}
