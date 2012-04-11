/*** dseq.c -- like seq(1) but for dates
 *
 * Copyright (C) 2009 - 2011 Sebastian Freundt
 *
 * Author:  Sebastian Freundt <freundt@ga-group.nl>
 *
 * This file is part of dateutils.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the author nor the names of any contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***/

#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include "dt-core.h"
#include "dt-io.h"
#include "tzraw.h"

typedef uint8_t __skipspec_t;

/* generic closure */
struct dseq_clo_s {
	struct dt_dt_s fst;
	struct dt_dt_s lst;
	struct dt_dt_s *ite;
	size_t nite;
	struct dt_dt_s *altite;
	__skipspec_t ss;
	size_t naltite;
	/* direction, >0 if increasing, <0 if decreasing, 0 if undefined */
	int dir;
	int flags;
#define CLO_FL_FREE_ITE		(1)
};


/* skip system */
static int
skipp(__skipspec_t ss, struct dt_dt_s dt)
{
	dt_dow_t dow;
	/* common case first */
	if (ss == 0) {
		return 0;
	}
	dow = dt_get_wday(dt.d);
	/* just check if the bit in the bitset `skip' is set */
	return (ss & (1 << dow)) != 0;
}

#define SKIP_MON	(2)
#define SKIP_TUE	(4)
#define SKIP_WED	(8)
#define SKIP_THU	(16)
#define SKIP_FRI	(32)
#define SKIP_SAT	(64)
#define SKIP_SUN	(1)

static inline int
__toupper(int c)
{
	return c & ~0x20;
}

static dt_dow_t
__parse_wd(const char *str)
{
#define ILEA(a, b)	(((a) << 8) | (b))
	int s1 = __toupper(str[0]);
	int s2 = __toupper(str[1]);

	switch (ILEA(s1, s2)) {
	case ILEA('M', 'O'):
	case ILEA('M', 0):
		/* monday */
		return DT_MONDAY;
	case ILEA('T', 'U'):
		/* tuesday */
		return DT_TUESDAY;
	case ILEA('W', 'E'):
	case ILEA('W', 0):
		/* wednesday */
		return DT_WEDNESDAY;
	case ILEA('T', 'H'):
		/* thursday */
		return DT_THURSDAY;
	case ILEA('F', 'R'):
	case ILEA('F', 0):
		/* friday */
		return DT_FRIDAY;
	case ILEA('S', 'A'):
	case ILEA('A', 0):
		/* saturday */
		return DT_SATURDAY;
	case ILEA('S', 'U'):
	case ILEA('S', 0):
		/* sunday */
		return DT_SUNDAY;
	default:
		return DT_MIRACLEDAY;
	}
}

static __skipspec_t
__skip_dow(__skipspec_t ss, dt_dow_t wd)
{
	switch (wd) {
	case DT_MONDAY:
		/* monday */
		ss |= SKIP_MON;
		break;
	case DT_TUESDAY:
		/* tuesday */
		ss |= SKIP_TUE;
		break;
	case DT_WEDNESDAY:
		/* wednesday */
		ss |= SKIP_WED;
		break;
	case DT_THURSDAY:
		/* thursday */
		ss |= SKIP_THU;
		break;
	case DT_FRIDAY:
		/* friday */
		ss |= SKIP_FRI;
		break;
	case DT_SATURDAY:
		/* saturday */
		ss |= SKIP_SAT;
		break;
	case DT_SUNDAY:
		/* sunday */
		ss |= SKIP_SUN;
		break;
	default:
	case DT_MIRACLEDAY:
		break;
	}
	return ss;
}

static __skipspec_t
__skip_str(__skipspec_t ss, const char *str)
{
	dt_dow_t tmp;

	if ((tmp = __parse_wd(str)) < DT_MIRACLEDAY) {
		ss = __skip_dow(ss, tmp);
	} else {
		int s1 = __toupper(str[0]);
		int s2 = __toupper(str[1]);

		if (ILEA(s1, s2) == ILEA('S', 'S')) {
			/* weekend */
			ss |= SKIP_SAT;
			ss |= SKIP_SUN;
		}
	}
	return ss;
}

static __skipspec_t
__skip_1spec(__skipspec_t ss, char *spec)
{
	char *tmp;
	dt_dow_t from, till;

	if ((tmp = strchr(spec, '-')) == NULL) {
		return __skip_str(ss, spec);
	}
	/* otherwise it's a range */
	*tmp = '\0';
	from = __parse_wd(spec);
	till = __parse_wd(tmp + 1);
	for (int d = from, e = till >= from ? till : till + 7; d <= e; d++) {
		ss = __skip_dow(ss, (dt_dow_t)(d % 7));
	}
	return ss;
}

static __skipspec_t
set_skip(__skipspec_t ss, char *spec)
{
	char *tmp, *tm2;

	if ((tmp = strchr(spec, ',')) == NULL) {
		return __skip_1spec(ss, spec);
	}
	/* const violation */
	*tmp++ = '\0';
	ss = __skip_1spec(ss, spec);
	while ((tmp = strchr(tm2 = tmp, ','))) {
		*tmp++ = '\0';
		ss = __skip_1spec(ss, tm2);
	}
	return __skip_1spec(ss, tm2);
}

static struct dt_dt_s
date_add(struct dt_dt_s d, struct dt_dt_s dur[], size_t ndur)
{
	for (size_t i = 0; i < ndur; i++) {
		d = dt_dtadd(d, dur[i]);
	}
	return d;
}

static void
date_neg_dur(struct dt_dt_s dur[], size_t ndur)
{
	for (size_t i = 0; i < ndur; i++) {
		dur[i] = dt_neg_dtdur(dur[i]);
	}
	return;
}

static bool
__daisy_feasible_p(struct dt_dt_s dur[], size_t ndur)
{
	if (ndur != 1) {
		return false;
	} else if (dur->typ == (dt_dttyp_t)DT_YMD && dur->d.ymd.m) {
		return false;
	} else if (dur->typ == (dt_dttyp_t)DT_BIZDA && (dur->d.bizda.bd)) {
		return false;
	}
	return true;
}

static bool
__dur_naught_p(struct dt_dt_s dur)
{
	return dur.d.u == 0 && dur.t.sdur == 0;
}

static bool
__durstack_naught_p(struct dt_dt_s dur[], size_t ndur)
{
	if (ndur == 0) {
		return true;
	} else if (ndur == 1) {
		return __dur_naught_p(dur[0]);
	}
	for (size_t i = 0; i < ndur; i++) {
		if (!__dur_naught_p(dur[i])) {
		    return false;
		}
	}
	return true;
}

static bool
__in_range_p(struct dt_dt_s now, struct dseq_clo_s *clo)
{
	if (!dt_sandwich_only_t_p(now)) {
		return (dt_dt_in_range_p(now, clo->fst, clo->lst) ||
			dt_dt_in_range_p(now, clo->lst, clo->fst));
	}
	/* otherwise perform a simple range check */
	if (clo->dir > 0) {
		if (now.t.u >= clo->fst.t.u && now.t.u <= clo->lst.t.u) {
			return true;
		} else if (clo->fst.t.u >= clo->lst.t.u) {
			return now.t.u <= clo->lst.t.u || now.d.daisydur == 0;
		}
	} else if (clo->dir < 0) {
		if (now.t.u <= clo->fst.t.u && now.t.u >= clo->lst.t.u) {
			return true;
		} else if (clo->fst.t.u <= clo->lst.t.u) {
			return now.t.u >= clo->lst.t.u || now.d.daisydur == 0;
		}
	}
	return false;
}

static struct dt_dt_s
__seq_altnext(struct dt_dt_s now, struct dseq_clo_s *clo)
{
	do {
		now = date_add(now, clo->altite, clo->naltite);
	} while (skipp(clo->ss, now) && __in_range_p(now, clo));
	return now;
}

static struct dt_dt_s
__seq_this(struct dt_dt_s now, struct dseq_clo_s *clo)
{
/* if NOW is on a skip date, find the next date according to ALTITE, then ITE */
	if (!skipp(clo->ss, now) && __in_range_p(now, clo)) {
		return now;
	} else if (clo->naltite > 0) {
		return __seq_altnext(now, clo);
	} else if (clo->nite) {
		/* advance until it goes out of range */
		for (;
		     skipp(clo->ss, now) && __in_range_p(now, clo);
		     now = date_add(now, clo->ite, clo->nite));
	} else {
		/* good question */
		;
	}
	return now;
}

static struct dt_dt_s
__seq_next(struct dt_dt_s now, struct dseq_clo_s *clo)
{
/* advance NOW, then fix it */
	struct dt_dt_s tmp = date_add(now, clo->ite, clo->nite);
	return __seq_this(tmp, clo);
}

static int
__get_dir(struct dt_dt_s d, struct dseq_clo_s *clo)
{
	struct dt_dt_s tmp;

	if (!dt_sandwich_only_t_p(d)) {
		/* trial addition to to see where it goes */
		tmp = __seq_next(d, clo);
		return dt_dtcmp(tmp, d);
	}
	if (clo->ite->t.sdur && !clo->ite->t.neg) {
		return 1;
	} else if (clo->ite->t.sdur && clo->ite->t.neg) {
		return -1;
	}
	return 0;
}

static struct dt_dt_s
__fixup_fst(struct dseq_clo_s *clo)
{
	struct dt_dt_s tmp;
	struct dt_dt_s old;

	/* assume clo->dir has been computed already */
	old = tmp = clo->lst;
	date_neg_dur(clo->ite, clo->nite);
	while (__in_range_p(tmp, clo)) {
		old = tmp;
		tmp = __seq_next(tmp, clo);
	}
	/* final checks */
	old = __seq_this(old, clo);
	date_neg_dur(clo->ite, clo->nite);
	/* fixup again with negated dur */
	old = __seq_this(old, clo);
	return old;
}

static struct dt_t_s
tseq_guess_ite(struct dt_t_s beg, struct dt_t_s end)
{
	struct dt_t_s res;

	if (beg.hms.h != end.hms.h &&
	    beg.hms.m == 0 && end.hms.m == 0&&
	    beg.hms.s == 0 && end.hms.s == 0) {
		if (beg.u < end.u) {
			res.sdur = SECS_PER_HOUR;
		} else {
			res.sdur = -SECS_PER_HOUR;
		}
	} else if (beg.hms.m != end.hms.m &&
		   beg.hms.s == 0 && end.hms.s == 0) {
		if (beg.u < end.u) {
			res.sdur = SECS_PER_MIN;
		} else {
			res.sdur = -SECS_PER_MIN;
		}
	} else {
		if (beg.u < end.u) {
			res.sdur = 1L;
		} else {
			res.sdur = -1L;
		}
	}
	res.dur = 1;
	return res;
}


#if defined __INTEL_COMPILER
# pragma warning (disable:593)
# pragma warning (disable:181)
#elif defined __GNUC__
# pragma GCC diagnostic ignored "-Wswitch-enum"
#endif	/* __INTEL_COMPILER */
#include "dseq-clo.h"
#include "dseq-clo.c"
#if defined __INTEL_COMPILER
# pragma warning (default:593)
# pragma warning (default:181)
#elif defined __GNUC__
# pragma GCC diagnostic warning "-Wswitch-enum"
#endif	/* __INTEL_COMPILER */

int
main(int argc, char *argv[])
{
	static struct dt_dt_s ite_p1;
	struct gengetopt_args_info argi[1];
	struct dt_dt_s tmp;
	char **ifmt;
	size_t nifmt;
	char *ofmt;
	int res = 0;
	struct dseq_clo_s clo = {
		.ite = &ite_p1,
		.nite = 1,
		.altite = NULL,
		.naltite = 0,
		.ss = 0,
		.dir = 0,
		.flags = 0,
	};

	/* fixup negative numbers, A -1 B for dates A and B */
	fixup_argv(argc, argv, NULL);
	if (cmdline_parser(argc, argv, argi)) {
		res = 1;
		goto out;
	}
	/* assign ofmt/ifmt */
	ofmt = argi->format_arg;
	if (argi->backslash_escapes_given) {
		dt_io_unescape(ofmt);
	}
	nifmt = argi->input_format_given;
	ifmt = argi->input_format_arg;

	for (size_t i = 0; i < argi->skip_given; i++) {
		clo.ss = set_skip(clo.ss, argi->skip_arg[i]);
	}

	if (argi->alt_inc_given) {
		struct __strpdtdur_st_s st = {0};

		unfixup_arg(argi->alt_inc_arg);
		do {
			if (dt_io_strpdtdur(&st, argi->alt_inc_arg) < 0) {
				if (!argi->quiet_given) {
					fprintf(stderr, "Error: \
cannot parse duration string `%s'\n", argi->alt_inc_arg);
				}
				res = 1;
				goto out;
			}
		} while (__strpdtdur_more_p(&st));
		/* assign values */
		clo.altite = st.durs;
		clo.naltite = st.ndurs;
	}

	switch (argi->inputs_num) {
		struct dt_dt_s fst, lst;
	default:
		cmdline_parser_print_help();
		res = 1;
		goto out;

	case 2:
		lst = dt_io_strpdt(argi->inputs[1], ifmt, nifmt, NULL);
		if (dt_unk_p(lst)) {
			if (!argi->quiet_given) {
				dt_io_warn_strpdt(argi->inputs[1]);
			}
			res = 1;
			goto out;
		}
		/* fallthrough */
	case 1:
		fst = dt_io_strpdt(argi->inputs[0], ifmt, nifmt, NULL);
		if (dt_unk_p(fst)) {
			if (!argi->quiet_given) {
				dt_io_warn_strpdt(argi->inputs[0]);
			}
			res = 1;
			goto out;
		}

		/* check the input arguments and do the sane thing now
		 * if it's all dates, use daisy iterator
		 * if it's all times, use sdur iterator
		 * if one of them is a dt, promote the other */
		if (dt_sandwich_only_d_p(fst)) {
			/* emulates old dseq(1) */
			if (argi->inputs_num == 1) {
				lst.d = dt_date(DT_YMD);
			}

			dt_make_d_only(&ite_p1, DT_DAISY);
			ite_p1.d.daisy = 1;
		} else if (dt_sandwich_only_t_p(fst)) {
			/* emulates old tseq(1) */
			if (argi->inputs_num == 1) {
				lst.t = dt_time();
			}
		} else if (dt_sandwich_p(fst)) {
			if (argi->inputs_num == 1) {
				lst = dt_datetime((dt_dttyp_t)DT_YMD);
			}

			dt_make_sandwich(&ite_p1, DT_DAISY, DT_TUNK);
			ite_p1.d.daisy = 1;
		} else {
			fputs("\
don't know how to handle single argument case\n", stderr);
			res = 1;
			goto out;
		}

		clo.fst = fst;
		clo.lst = lst;
		break;
	case 3: {
		struct __strpdtdur_st_s st = {0};

		/* get lower bound */
		fst = dt_io_strpdt(argi->inputs[0], ifmt, nifmt, NULL);
		if (dt_unk_p(fst)) {
			if (!argi->quiet_given) {
				dt_io_warn_strpdt(argi->inputs[0]);
			}
			res = 1;
			goto out;
		}

		/* get increment */
		unfixup_arg(argi->inputs[1]);
		do {
			if (dt_io_strpdtdur(&st, argi->inputs[1]) < 0) {
				fprintf(stderr, "Error: \
cannot parse duration string `%s'\n", argi->inputs[1]);
				res = 1;
				goto out;
			}
		} while (__strpdtdur_more_p(&st));
		/* assign values */
		clo.ite = st.durs;
		clo.nite = st.ndurs;
		clo.flags |= CLO_FL_FREE_ITE;

		/* get upper bound */
		lst = dt_io_strpdt(argi->inputs[2], ifmt, nifmt, NULL);
		if (dt_unk_p(lst)) {
			if (!argi->quiet_given) {
				dt_io_warn_strpdt(argi->inputs[2]);
			}
			res = 1;
			goto out;
		}
		clo.fst = fst;
		clo.lst = lst;
		break;
	}
	}

	/* promote the args maybe */
	if ((dt_sandwich_only_d_p(clo.fst) && dt_sandwich_only_t_p(clo.lst)) ||
	    (dt_sandwich_only_t_p(clo.fst) && dt_sandwich_only_d_p(clo.lst))) {
		fputs("\
cannot mix dates and times as arguments\n", stderr);
		res = 1;
		goto out;
	} else if (dt_sandwich_only_d_p(clo.fst) && dt_sandwich_p(clo.lst)) {
		/* promote clo.fst */
		clo.fst.t = clo.lst.t;
		dt_make_sandwich(&clo.fst, clo.fst.d.typ, clo.lst.t.typ);
	} else if (dt_sandwich_p(clo.fst) && dt_sandwich_only_d_p(clo.lst)) {
		/* promote clo.lst */
		clo.lst.t = clo.fst.t;
		dt_make_sandwich(&clo.lst, clo.lst.d.typ, clo.fst.t.typ);
	} else if (dt_sandwich_only_t_p(clo.fst) && dt_sandwich_p(clo.lst)) {
		/* promote clo.fst */
		clo.fst.d = clo.lst.d;
		dt_make_sandwich(&clo.fst, clo.fst.d.typ, clo.lst.t.typ);
	} else if (dt_sandwich_p(clo.fst) && dt_sandwich_only_t_p(clo.lst)) {
		/* promote clo.lst */
		clo.lst.d = clo.fst.d;
		dt_make_sandwich(&clo.lst, clo.lst.d.typ, clo.fst.t.typ);
	}

	/* convert to daisies */
	if (dt_sandwich_only_d_p(clo.fst) &&
	    __daisy_feasible_p(clo.ite, clo.nite) &&
	    ((clo.fst = dt_dtconv(DT_DAISY, clo.fst)).d.typ != DT_DAISY ||
	     (clo.lst = dt_dtconv(DT_DAISY, clo.lst)).d.typ != DT_DAISY)) {
		if (!argi->quiet_given) {
			fputs("\
cannot convert calendric system internally\n", stderr);
		}
		res = 1;
		goto out;
	} else if (dt_sandwich_only_t_p(clo.fst) && clo.ite->t.sdur == 0) {
		clo.ite->t = tseq_guess_ite(clo.fst.t, clo.lst.t);
	}

	if (__durstack_naught_p(clo.ite, clo.nite) ||
	    (clo.dir = __get_dir(clo.fst, &clo)) == 0) {
		if (!argi->quiet_given) {
			fputs("\
increment must not be naught\n", stderr);
		}
		res = 1;
		goto out;
	} else if (argi->compute_from_last_given) {
		tmp = __fixup_fst(&clo);
	} else {
		tmp = __seq_this(clo.fst, &clo);
	}

	for (; __in_range_p(tmp, &clo); tmp = __seq_next(tmp, &clo)) {
		dt_io_write(tmp, ofmt, NULL);
	}

out:
	/* free strpdur resources */
	if (clo.ite && clo.flags & CLO_FL_FREE_ITE) {
		free(clo.ite);
	}
	if (clo.altite) {
		free(clo.altite);
	}
	cmdline_parser_free(argi);
	return res;
}

/* dseq.c ends here */