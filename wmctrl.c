/* license {{{ */
/*

wmctrl
A command line tool to interact with an EWMH/NetWM compatible X Window Manager.

Author, current maintainer: Tomas Styblo <tripie@cpan.org>

Copyright (C) 2003

This program is free software which I release under the GNU General Public
License. You may redistribute and/or modify this program under the terms
of that license as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

To get a copy of the GNU General Puplic License,  write to the
Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
/* }}} */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "wmctrl.h"

#define MAX_PROPERTY_VALUE_LEN 4096

#define p_verbose(...) g_message(__VA_ARGS__)

static gchar *get_property (Display *disp, Window win, /*{{{*/
        Atom xa_prop_type, gchar *prop_name, unsigned long *size) {
    Atom xa_prop_name;
    Atom xa_ret_type;
    int ret_format;
    unsigned long ret_nitems;
    unsigned long ret_bytes_after;
    unsigned long tmp_size;
    unsigned char *ret_prop;
    gchar *ret;

    xa_prop_name = XInternAtom(disp, prop_name, False);

    /* MAX_PROPERTY_VALUE_LEN / 4 explanation (XGetWindowProperty manpage):
     *
     * long_length = Specifies the length in 32-bit multiples of the
     *               data to be retrieved.
     *
     * Description: Correct 64 Architecture implementation of 32 bit data
     * Author: Chris Donoghue <cdonoghu@gmail.com>
     * Bug-Debian: http://bugs.debian.org/362068
     *
     * NOTE:  see
     * http://mail.gnome.org/archives/wm-spec-list/2003-March/msg00067.html
     * In particular:
     *
     *   When the X window system was ported to 64-bit architectures, a
     * rather peculiar design decision was made. 32-bit quantities such
     * as Window IDs, atoms, etc, were kept as longs in the client side
     * APIs, even when long was changed to 64 bits.
     *
     */
    if (XGetWindowProperty(disp, win, xa_prop_name, 0, MAX_PROPERTY_VALUE_LEN / 4, False,
            xa_prop_type, &xa_ret_type, &ret_format,
            &ret_nitems, &ret_bytes_after, &ret_prop) != Success) {
        p_verbose("Cannot get %s property.\n", prop_name);
        return NULL;
    }

    if (xa_ret_type != xa_prop_type) {
        p_verbose("Invalid type of %s property.\n", prop_name);
        XFree(ret_prop);
        return NULL;
    }

    /* null terminate the result to make string handling easier */
    tmp_size = (ret_format / 8) * ret_nitems;
    /* Correct 64 Architecture implementation of 32 bit data */
    if(ret_format==32) tmp_size *= sizeof(long)/4;
    ret = g_malloc(tmp_size + 1);
    memcpy(ret, ret_prop, tmp_size);
    ret[tmp_size] = '\0';

    if (size) {
        *size = tmp_size;
    }

    XFree(ret_prop);
    return ret;
}/*}}}*/

Window* get_client_list (Display *disp, unsigned long *size) {/*{{{*/
    Window *client_list;

    if ((client_list = (Window *)get_property(disp, DefaultRootWindow(disp),
                    XA_WINDOW, "_NET_CLIENT_LIST", size)) == NULL) {
        if ((client_list = (Window *)get_property(disp, DefaultRootWindow(disp),
                        XA_CARDINAL, "_WIN_CLIENT_LIST", size)) == NULL) {
            g_error("Cannot get client list properties. \n"
                  "(_NET_CLIENT_LIST or _WIN_CLIENT_LIST)");
            return NULL;
        }
    }

    return client_list;
}/*}}}*/

