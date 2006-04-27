%{
/* ----------------------------------------------------------------------- *
 *   
 *  "$Id: nss_tok.l,v 1.3 2006/03/21 04:28:53 raven Exp $"
 *
 *  nss_tok.l - nsswitch tokenizer.
 *
 *   Copyright 2006 Ian Kent <raven@themaw.net>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *   
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 * ----------------------------------------------------------------------- */

#ifdef ECHO
# undef ECHO
#endif /* ECHO */
static void nss_echo(void);	/* forward definition */
#define ECHO nss_echo()

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nss_parse.tab.h"

/*
 * There are some things that need to be defined only if useing GNU flex.
 * These must not be defined if using standard lex
 */
#ifdef FLEX_SCANNER
int nss_lineno;
#endif

int nss_lex(void);
#ifndef nss_wrap
int nss_wrap(void);
#endif
#define YY_SKIP_YYWRAP

%}

%option nounput

%x AUTOMOUNT

WS		[[:blank:]]+

automount	([Aa][Uu][Tt][Oo][Mm][Oo][Uu][Nn][Tt])

source		files|yp|nis|nisplus|ldap|hesiod

success		([Ss][Uu][Cc][Cc][Ee][Ss][Ss])
notfound	([Nn][Oo][Tt][Ff][Oo][Uu][Nn][Dd])
unavail		([Uu][Nn][Aa][Vv][Aa][Ii][Ll])
tryagain	([Tt][Rr][Yy][Aa][Gg][Aa][Ii][Nn])

status		({success}|{notfound}|{unavail}|{tryagain})

continue	([Cc][Oo][Nn][Tt][Ii][Nn][Uu][Ee])
return		([Rr][Ee][Tt][Uu][Rr][Nn])

action		({continue}|{return})

other		[[:alnum:]@$%^&*()-+_":;?,<>./'{}~`]+

%%

^{automount}: {
	BEGIN(AUTOMOUNT);
}

\n|.	{}

<AUTOMOUNT>{
	{WS}	{ }

	{source} {
		strcpy(nss_lval.strval, nss_text);
		return SOURCE;
	}

	{status} {
		strcpy(nss_lval.strval, nss_text);
		return STATUS;
	}

	{action} {
		strcpy(nss_lval.strval, nss_text);
		return ACTION;
	}

	"["	{ return LBRACKET; }
	"]"	{ return RBRACKET; }
	"="	{ return EQUAL; }
	"!"	{ return BANG; }

	{other} {
		strcpy(nss_lval.strval, nss_text);
		return OTHER;
	}

	\n	{ BEGIN(INITIAL); return NL; }
}

%%

#include "automount.h"

int nss_wrap(void)
{
	return 1;
}

static void nss_echo(void)
{
	msg("%s", nss_text);
	return;
}