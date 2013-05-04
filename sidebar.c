/*
 * Copyright (C) ????-2004 Justin Hibbits <jrh29@po.cwru.edu>
 * Copyright (C) 2004 Thomer M. Gil <mutt@thomer.com>
 * 
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */ 


#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mutt.h"
#include "mutt_menu.h"
#include "mutt_curses.h"
#include "sidebar.h"
#include "buffy.h"
#include <libgen.h>
#include "keymap.h"
#include <stdbool.h>

/*BUFFY *CurBuffy = 0;*/
static BUFFY *TopBuffy = 0;
static BUFFY *BottomBuffy = 0;
static int known_lines = 0;

void calc_boundaries() {

    BUFFY *tmp = Incoming;

	int count = LINES - 2 - (option(OPTHELP) ? 1 : 0);

	if ( known_lines != LINES ) {
		TopBuffy = BottomBuffy = 0;
		known_lines = LINES;
	}
	for ( ; tmp->next != 0; tmp = tmp->next )
		tmp->next->prev = tmp;

	if ( TopBuffy == 0 && BottomBuffy == 0 )
		TopBuffy = Incoming;
	if ( BottomBuffy == 0 ) {
		BottomBuffy = TopBuffy;
		while ( --count && BottomBuffy->next )
			BottomBuffy = BottomBuffy->next;
	}
	else if ( TopBuffy == CurBuffy->next ) {
		BottomBuffy = CurBuffy;
		tmp = BottomBuffy;
		while ( --count && tmp->prev)
			tmp = tmp->prev;
		TopBuffy = tmp;
	}
	else if ( BottomBuffy == CurBuffy->prev ) {
		TopBuffy = CurBuffy;
		tmp = TopBuffy;
		while ( --count && tmp->next )
			tmp = tmp->next;
		BottomBuffy = tmp;
	}
}

static const char *
sidebar_format_str (char *dest,
			size_t destlen,
			size_t col,
			char op,
			const char *src,
			const char *prefix,
			const char *ifstring,
			const char *elsestring,
			unsigned long data,
			format_flag flags)
{
/* casting from unsigned long - srsly?! */
struct sidebar_entry *sbe = (struct sidebar_entry *) data;
unsigned int optional;
char fmt[SHORT_STRING], buf[SHORT_STRING];

optional = flags & M_FORMAT_OPTIONAL;

switch(op) {
	case 'F':
		if(!optional) {
			snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
			snprintf (dest, destlen, fmt, sbe->flagged);
		} else if(sbe->flagged == 0) {
			optional = 0;
		}
		break;

	case '!':
		if(sbe->flagged == 0)
			mutt_format_s(dest, destlen, prefix, "");
		if(sbe->flagged == 1)
			mutt_format_s(dest, destlen, prefix, "!");
		if(sbe->flagged == 2)
			mutt_format_s(dest, destlen, prefix, "!!");
		if(sbe->flagged > 2) {
			snprintf (buf, sizeof (buf), "%d!", sbe->flagged);
			mutt_format_s(dest, destlen, prefix, buf);
		}
		break;

	case 'S':
		snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
		snprintf (dest, destlen, fmt, sbe->size);
		break;

	case 'N':
		if(!optional) {
			snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
			snprintf (dest, destlen, fmt, sbe->new);
		} else if(sbe->new == 0) {
			optional = 0;
		}
		break;

	case 'B':
		mutt_format_s(dest, destlen, prefix, sbe->box);
		break;
	}

	if(optional)
		mutt_FormatString (dest, destlen, col, ifstring, sidebar_format_str, (unsigned long) sbe, flags);
	else if (flags & M_FORMAT_OPTIONAL)
		mutt_FormatString (dest, destlen, col, elsestring, sidebar_format_str, (unsigned long) sbe, flags);

	return (src);
}

char *make_sidebar_entry(char *box, unsigned int size, unsigned int new, unsigned int flagged) {
    static char *entry = 0;
    struct sidebar_entry sbe;
    int SBvisual;

    SBvisual = SidebarWidth - strlen(SidebarDelim);
    if (SBvisual < 1)
        return NULL;

    sbe.new = new;
    sbe.flagged = flagged;
    sbe.size = size;
    strncpy(sbe.box, box, 31);

    safe_realloc(&entry, SBvisual + 2);
    entry[SBvisual + 1] = '\0';

    mutt_FormatString (entry, SBvisual+1, 0, SidebarFormat, sidebar_format_str, (unsigned long) &sbe, 0);

    return entry;
}

void set_curbuffy(char buf[LONG_STRING])
{
  BUFFY* tmp = CurBuffy = Incoming;

  if (!Incoming)
    return;

  while(1) {
    if(!strcmp(tmp->path, buf) || !strcmp(tmp->realpath, buf)) {
      CurBuffy = tmp;
      break;
    }

    if(tmp->next)
      tmp = tmp->next;
    else
      break;
  }
}

int draw_sidebar(int menu) {

	BUFFY *tmp;
#ifndef USE_SLANG_CURSES
        attr_t attrs;
#endif
        short delim_len = strlen(SidebarDelim);
        short color_pair;

        static bool initialized = false;
        static int prev_show_value;
        static short saveSidebarWidth;
        int lines = 0;
        int SidebarHeight;
        
        if(option(OPTSTATUSONTOP) || option(OPTHELP))
                lines++; /* either one will occupy the first line */

        /* initialize first time */
        if(!initialized) {
                prev_show_value = option(OPTSIDEBAR);
                saveSidebarWidth = SidebarWidth;
                if(!option(OPTSIDEBAR)) SidebarWidth = 0;
                initialized = true;
        }

        /* save or restore the value SidebarWidth */
        if(prev_show_value != option(OPTSIDEBAR)) {
                if(prev_show_value && !option(OPTSIDEBAR)) {
                        saveSidebarWidth = SidebarWidth;
                        SidebarWidth = 0;
                } else if(!prev_show_value && option(OPTSIDEBAR)) {
                        mutt_buffy_check(1); /* we probably have bad or no numbers */
                        SidebarWidth = saveSidebarWidth;
                }
                prev_show_value = option(OPTSIDEBAR);
        }


/*	if ( SidebarWidth == 0 ) return 0; */
       if (SidebarWidth > 0 && option (OPTSIDEBAR)
           && delim_len >= SidebarWidth) {
         unset_option (OPTSIDEBAR);
         /* saveSidebarWidth = SidebarWidth; */
         if (saveSidebarWidth > delim_len) {
           SidebarWidth = saveSidebarWidth;
           mutt_error (_("Value for sidebar_delim is too long. Disabling sidebar."));
           sleep (2);
         } else {
           SidebarWidth = 0;
           mutt_error (_("Value for sidebar_delim is too long. Disabling sidebar. Please set your sidebar_width to a sane value."));
           sleep (4); /* the advise to set a sane value should be seen long enough */
         }
         saveSidebarWidth = 0;
         return (0);
       }

    if ( SidebarWidth == 0 || !option(OPTSIDEBAR)) {
      if (SidebarWidth > 0) {
        saveSidebarWidth = SidebarWidth;
        SidebarWidth = 0;
      }
      unset_option(OPTSIDEBAR);
      return 0;
    }

        /* get attributes for divider */
	SETCOLOR(MT_COLOR_STATUS);
#ifndef USE_SLANG_CURSES
        attr_get(&attrs, &color_pair, 0);
#else
        color_pair = attr_get();
#endif
	SETCOLOR(MT_COLOR_NORMAL);

	/* draw the divider */

	SidebarHeight =  LINES - 1;
	if(option(OPTHELP) || !option(OPTSTATUSONTOP))
		SidebarHeight--;

	for ( ; lines < SidebarHeight; lines++ ) {
		move(lines, SidebarWidth - delim_len);
		addstr(NONULL(SidebarDelim));
#ifndef USE_SLANG_CURSES
                mvchgat(lines, SidebarWidth - delim_len, delim_len, 0, color_pair, NULL);
#endif
	}

	if ( Incoming == 0 ) return 0;
        lines = 0;
        if(option(OPTSTATUSONTOP) || option(OPTHELP))
                lines++; /* either one will occupy the first line */

	if ( known_lines != LINES || TopBuffy == 0 || BottomBuffy == 0 ) 
		calc_boundaries(menu);
	if ( CurBuffy == 0 ) CurBuffy = Incoming;

	tmp = TopBuffy;

	SETCOLOR(MT_COLOR_NORMAL);

	for ( ; tmp && lines < SidebarHeight; tmp = tmp->next ) {
		if ( tmp == CurBuffy )
			SETCOLOR(MT_COLOR_INDICATOR);
		else if ( tmp->msg_unread > 0 )
			SETCOLOR(MT_COLOR_NEW);
		else if ( tmp->msg_flagged > 0 )
		        SETCOLOR(MT_COLOR_FLAGGED);
		else
			SETCOLOR(MT_COLOR_NORMAL);

		move( lines, 0 );
		if ( Context && (!strcmp(tmp->path, Context->path)||
				 !strcmp(tmp->realpath, Context->path)) ) {
			tmp->msg_unread = Context->unread;
			tmp->msgcount = Context->msgcount;
			tmp->msg_flagged = Context->flagged;
		}
		/* check whether Maildir is a prefix of the current folder's path */
		short maildir_is_prefix = 0;
		if ( (strlen(tmp->path) > strlen(Maildir)) &&
			(strncmp(Maildir, tmp->path, strlen(Maildir)) == 0) )
        		maildir_is_prefix = 1;
		/* calculate depth of current folder and generate its display name with indented spaces */
		int sidebar_folder_depth = 0;
		char *sidebar_folder_name;
		sidebar_folder_name = option(OPTSIDEBARSHORTPATH) ? mutt_basename(tmp->path) : tmp->path + maildir_is_prefix*(strlen(Maildir) + 1);
		if ( maildir_is_prefix && option(OPTSIDEBARFOLDERINDENT) ) {
			char *tmp_folder_name;
			int i;
			tmp_folder_name = tmp->path + strlen(Maildir) + 1;
			for (i = 0; i < strlen(tmp->path) - strlen(Maildir); i++) {
 				if (tmp_folder_name[i] == '/'  || tmp_folder_name[i] == '.') sidebar_folder_depth++;
			}   
			if (sidebar_folder_depth > 0) {
 				if (option(OPTSIDEBARSHORTPATH)) {
 					tmp_folder_name = strrchr(tmp->path, '.');
 					if (tmp_folder_name == NULL)
 						tmp_folder_name = mutt_basename(tmp->path);
 					else
						tmp_folder_name++;
 				}
 				else
 					tmp_folder_name = tmp->path + strlen(Maildir) + 1;
 				sidebar_folder_name = malloc(strlen(tmp_folder_name) + sidebar_folder_depth*strlen(NONULL(SidebarIndentStr)) + 1);
				sidebar_folder_name[0]=0;
				for (i=0; i < sidebar_folder_depth; i++)
					strncat(sidebar_folder_name, NONULL(SidebarIndentStr), strlen(NONULL(SidebarIndentStr)));
				strncat(sidebar_folder_name, tmp_folder_name, strlen(tmp_folder_name));
			}
		}
		printw( "%.*s", SidebarWidth - delim_len + 1,
			make_sidebar_entry(sidebar_folder_name, tmp->msgcount,
			tmp->msg_unread, tmp->msg_flagged));
		if (sidebar_folder_depth > 0)
		        free(sidebar_folder_name);
		lines++;
	}
	SETCOLOR(MT_COLOR_NORMAL);
	for ( ; lines < SidebarHeight; lines++ ) {
		int i = 0;
		move( lines, 0 );
		for ( ; i < SidebarWidth - delim_len; i++ )
			addch(' ');
	}
	return 0;
}


void set_buffystats(CONTEXT* Context)
{
        BUFFY *tmp = Incoming;
        while(tmp) {
                if(Context && (!strcmp(tmp->path, Context->path) ||
                               !strcmp(tmp->realpath, Context->path))) {
			tmp->msg_unread = Context->unread;
			tmp->msgcount = Context->msgcount;
			tmp->msg_flagged = Context->flagged;
                        break;
                }
                tmp = tmp->next;
        }
}

void scroll_sidebar(int op, int menu)
{
        if(!SidebarWidth) return;
        if(!CurBuffy) return;

	switch (op) {
		case OP_SIDEBAR_NEXT:
			if ( CurBuffy->next == NULL ) return;
			CurBuffy = CurBuffy->next;
			break;
		case OP_SIDEBAR_PREV:
			if ( CurBuffy->prev == NULL ) return;
			CurBuffy = CurBuffy->prev;
			break;
		case OP_SIDEBAR_SCROLL_UP:
			CurBuffy = TopBuffy;
			if ( CurBuffy != Incoming ) {
				calc_boundaries(menu);
				CurBuffy = CurBuffy->prev;
			}
			break;
		case OP_SIDEBAR_SCROLL_DOWN:
			CurBuffy = BottomBuffy;
			if ( CurBuffy->next ) {
				calc_boundaries(menu);
				CurBuffy = CurBuffy->next;
			}
			break;
		default:
			return;
	}
	calc_boundaries(menu);
	draw_sidebar(menu);
}

