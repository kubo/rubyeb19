/**********************************************
*    eb.c
*    Ruby Extension Library for EB
*    $Id: eb.c,v 2.24 2004/07/12 17:45:15 nyasu3w Exp $
*    Author: NISHIKAWA Yasuhiro <nyasu3w@users.sourceforge.net>
*    http://rubyeb.sourceforge.net/
*    Copyleft
************************************************/

#define RUBYEB_VERSION "2.6"


#include "ruby.h"
#include <ruby/encoding.h>

#ifndef rb_iterator_p
#define rb_iterator_p() rb_block_given_p()
#endif


#if HAVE_EB_SYSDEFS_H
#include <eb/sysdefs.h>
#if (defined RUBY_EB_ENABLE_PTHREAD && !defined(EB_ENABLE_PTHREAD)) \
 || (!defined RUBY_EB_ENABLE_PTHREAD && defined(EB_ENABLE_PTHREAD))
#error The EB library is incompatible with EB heders.
#endif

#elif defined RUBY_EB_ENABLE_PTHREAD
#define EBCONF_ENABLE_PTHREAD 1
#endif

#include <eb/eb.h>
#include <eb/error.h>
#include <eb/text.h>
#include <eb/binary.h>
#include <eb/font.h>
#include <eb/appendix.h>


#if EB_VERSION_MAJOR < 3 || (EB_VERSION_MAJOR==3 && EB_VERSION_MINOR<2)
#error EB version too small.
#endif


/* eb_???_font_xbm_size() doesn't work on eb-3.2.3 */
#define HAVE_XBMSIZE_BUG  0



#include <stdlib.h>

/* my decided numbers */
#define MAX_HITS 50             /* max hit results */
#define MAX_STRLEN 65530        /* (^^); */
#define MAX_KEYWORDS 8          /* max keywords */
/* my decided numbers end */

#define SEARCHTYPE_WORD 1
#define SEARCHTYPE_WORDLIST 2

#define HOOKSET_EB_IVAR "__hookset"
#define HOOKSET_PROCS_IVAR "__hookprocs"

#define APPENDIX_EB_IVAR "__appendix"

#define REB_TO_RB_ENCODING(reb) rb_enc_from_index(NUM2INT(rb_ivar_get(reb, sym_eb_encidx)))

struct ExtFont {
    int code;
    int wideflag;               /* boolean */
    EB_Font_Code fontsize;      /* EB_FONT_?? */
    char bitmap[EB_SIZE_WIDE_FONT_48];
};


static EB_Error_Code eb_error;



static VALUE mEB;
static VALUE cEBook;
static VALUE cEBCancel;
static VALUE cEBPosition;
static VALUE cEBExtFont;
static VALUE cEBHook;
static VALUE cEBAppendix;

static VALUE sym_eb_encidx;

static ID id_call;

static int
text_hook(EB_Book * book, EB_Appendix * appendix, void *container, EB_Hook_Code code, int argc, const int *argv)
{
    VALUE func, ret_buff, rargv, rb_eb, rb_hookset;
    int idx;
    char *tmpbuffer;

    rb_eb = (VALUE) container;
    rb_hookset = rb_iv_get(rb_eb, HOOKSET_EB_IVAR);
    if (rb_hookset == Qnil) {
        return 0;
    }

    func = rb_ary_entry(rb_iv_get(rb_hookset, HOOKSET_PROCS_IVAR), code);

    rargv = rb_ary_new2(argc);
    for (idx = 0; idx < argc; idx++) {
        rb_ary_store(rargv, idx, INT2FIX(argv[idx]));
    }

    ret_buff = rb_funcall(func, id_call, 2, rb_eb, rargv);
    if (ret_buff != Qnil) {
        if (TYPE(ret_buff) == T_STRING) {
            ret_buff = rb_funcall(ret_buff, rb_intern("to_str"), 0);
        }
        tmpbuffer = StringValueCStr(ret_buff);
        eb_write_text_string(book, tmpbuffer);
    }
    return 0;
}

/**********
* Finalizers
*/

static void
finalize_book(EB_Book * eb)
{
    eb_finalize_book(eb);
    free(eb);
}
static void
finalize_hookset(EB_Hookset * hk)
{
    eb_finalize_hookset(hk);
    free(hk);
}
static void
finalize_appendix(EB_Appendix * apx)
{
    eb_finalize_appendix(apx);
    free(apx);
}

static EB_Appendix *
get_eb_appendix(VALUE reb)
{
    VALUE rebapx;
    EB_Appendix *appendix;

    rebapx = rb_iv_get(reb, APPENDIX_EB_IVAR);
    if (rebapx != Qnil) {
        Data_Get_Struct(rebapx, EB_Appendix, appendix);
    }
    else {
        appendix = NULL;
    }
    return appendix;
}

/******************************
**  EB Book
*/

static VALUE
reb_initialize(VALUE klass)
{
    VALUE robj, reb_appendix;
    EB_Book *eb;
    EB_Appendix *appendix;

    robj = Data_Make_Struct(klass, EB_Book, 0, finalize_book, eb);
    eb_initialize_book(eb);

    reb_appendix = Data_Make_Struct(cEBAppendix, EB_Appendix, 0, finalize_appendix, appendix);
    eb_initialize_appendix(appendix);
    rb_iv_set(robj, APPENDIX_EB_IVAR, reb_appendix);
    rb_ivar_set(robj, sym_eb_encidx, INT2FIX(rb_ascii8bit_encindex()));

    return robj;
}

static VALUE
rEB_error(VALUE module)
{
    return INT2NUM(eb_error);
}

static VALUE
rEB_errormsg(VALUE module)
{
    return rb_str_new2(eb_error_message(eb_error));
}

static VALUE
reb_bind(VALUE obj, VALUE path)
{
    EB_Book *eb;
    int r;
    EB_Character_Code charcode = EB_CHARCODE_INVALID;
    int encidx;

    Data_Get_Struct(obj, EB_Book, eb);
    r = eb_bind(eb, StringValueCStr(path));
    if (r != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "%s", eb_error_message(r));
        return Qfalse;
    }

    eb_character_code(eb, &charcode);
    switch (charcode) {
    case EB_CHARCODE_ISO8859_1:
        encidx = rb_enc_find_index("ISO-8859-1");
        break;
    case EB_CHARCODE_JISX0208:
        encidx = rb_enc_find_index("EUC-JP");
        break;
    default:
        encidx = rb_ascii8bit_encindex();
        break;
    }
    rb_ivar_set(obj, sym_eb_encidx, INT2FIX(encidx));

    return obj;
}

static VALUE
reb_disktype(VALUE obj)
{
    EB_Book *eb;
    EB_Disc_Code r;

    Data_Get_Struct(obj, EB_Book, eb);

    eb_error = eb_disc_type(eb, &r);
    switch (r) {
    case EB_DISC_EB:
        return rb_usascii_str_new_cstr("EB/EBG/EBXA/EBXA-C/S-EBXA");
        break;
    case EB_DISC_EPWING:
        return rb_usascii_str_new_cstr("EPWING");
        break;
    }
    return rb_usascii_str_new_cstr("Unknown");
}

static VALUE
reb_suspend(VALUE obj)
{
    EB_Book *eb;

    Data_Get_Struct(obj, EB_Book, eb);
    eb_suspend(eb);
    return Qnil;
}

static VALUE
reb_isbound(VALUE obj)
{
    EB_Book *eb;
    int r;

    Data_Get_Struct(obj, EB_Book, eb);
    r = eb_is_bound(eb);

    return (r) ? Qtrue : Qfalse;
}

static VALUE
reb_path(VALUE obj)
{
    EB_Book *eb;
    char r[1024];               /*絶対値はまずいと思う */

    Data_Get_Struct(obj, EB_Book, eb);
    eb_error = eb_path(eb, r);

    return rb_filesystem_str_new_cstr(r);
}

static VALUE
reb_charcode(VALUE obj)
{
    EB_Book *eb;
    EB_Character_Code r = EB_CHARCODE_INVALID;

    Data_Get_Struct(obj, EB_Book, eb);
    eb_error = eb_character_code(eb, &r);

    switch (r) {
    case EB_CHARCODE_ISO8859_1:
        return rb_usascii_str_new_cstr("ISO8859_1");
        break;
    case EB_CHARCODE_JISX0208:
        return rb_usascii_str_new_cstr("JISX0208");
        break;
    }
    return Qnil;
}

static VALUE
reb_subbookcount(VALUE obj)
{
    EB_Book *eb;
    int r;
    EB_Subbook_Code sbooks[EB_MAX_SUBBOOKS];

    Data_Get_Struct(obj, EB_Book, eb);
/* eb_subbook_count(eb,&r); */
    eb_subbook_list(eb, sbooks, &r);

    return INT2NUM(r);
}

static VALUE
reb_subbooklist(VALUE obj)
{
    EB_Book *eb;
    EB_Subbook_Code slist[EB_MAX_SUBBOOKS];
    int i, r;
    VALUE robj;

    Data_Get_Struct(obj, EB_Book, eb);
    eb_error = eb_subbook_list(eb, slist, &r);
    robj = rb_ary_new2(r);
    for (i = 0; i < r; i++)
        rb_ary_push(robj, INT2NUM(slist[i]));
    return robj;
}

static VALUE
reb_subbooktitle(int argc, VALUE * argv, VALUE obj)
{
    EB_Book *eb;
    char r[1024];               /*絶対値はまずいと思う */
    rb_encoding *enc = REB_TO_RB_ENCODING(obj);

    Data_Get_Struct(obj, EB_Book, eb);
    eb_error = (argc == 0) ?
        eb_subbook_title(eb, r) : eb_subbook_title2(eb, NUM2INT(argv[0]), r);

    return rb_external_str_new_with_enc(r, strlen(r), enc);
}

static VALUE
reb_subbookdirectory(int argc, VALUE * argv, VALUE obj)
{
    EB_Book *eb;
    char r[1024];               /*絶対値はまずいと思う */

    Data_Get_Struct(obj, EB_Book, eb);
    eb_error = (argc == 0) ?
        eb_subbook_directory(eb, r) : eb_subbook_directory2(eb, NUM2INT(argv[0]), r);
    return rb_str_new2(r);
}

static VALUE
reb_setsubbook(VALUE obj, VALUE sbook)
{
    EB_Book *eb;
    EB_Appendix *appendix;
    int r;
    int sub_code = NUM2INT(sbook);

    Data_Get_Struct(obj, EB_Book, eb);
    r = eb_set_subbook(eb, NUM2INT(sbook));
    if(r != EB_SUCCESS) return Qfalse;

    appendix = get_eb_appendix(obj);

    if (eb_is_appendix_bound(appendix)) {
        r = eb_set_appendix_subbook(appendix, sub_code);
        if (r != EB_SUCCESS) {
            rb_raise(rb_eRuntimeError, "eb_set_appendix_subbook() failed\n");
            return Qfalse;
        }
    }

    return obj;
}

static VALUE
reb_getsubbook(VALUE obj, VALUE sbook)
{
    EB_Book *eb;
    int r;

    Data_Get_Struct(obj, EB_Book, eb);
    eb_error = eb_subbook(eb, &r);

    return INT2NUM(r);
}

static VALUE
reb_unsetsubbook(VALUE obj)
{
    EB_Book *eb;

    Data_Get_Struct(obj, EB_Book, eb);
    eb_unset_subbook(eb);

    return obj;
}

static VALUE
have_search(VALUE obj, EB_Error_Code(*funct) (EB_Book *))
{
    EB_Book *eb;
    int r;
    Data_Get_Struct(obj, EB_Book, eb);
    r = (*funct) (eb);
    if (!r && eb_error == EB_ERR_NO_CUR_SUB) {
        rb_raise(rb_eRuntimeError, "%s", eb_error_message(eb_error));
        return Qfalse;
    }
    return (r) ? Qtrue : Qfalse;
}

static VALUE
reb_haveexactsearch(VALUE obj)
{
    return have_search(obj, eb_have_exactword_search);
}

static VALUE
reb_havewordsearch(VALUE obj)
{
    return have_search(obj, eb_have_word_search);
}
static VALUE
reb_haveendsearch(VALUE obj)
{
    return have_search(obj, eb_have_endword_search);
}

static VALUE
reb_havekeywordsearch(VALUE obj)
{
    return have_search(obj, eb_have_keyword_search);
}


/******************************
**  for result
*/

static EB_Hookset *
get_eb_texthook(VALUE reb)
{
    VALUE rebhook;
    EB_Hookset *text_hookset;

    rebhook = rb_iv_get(reb, HOOKSET_EB_IVAR);
    if (rebhook != Qnil) {
        Data_Get_Struct(rebhook, EB_Hookset, text_hookset);
    }
    else {
        text_hookset = NULL;
    }
    return text_hookset;
}

static VALUE
content_read(VALUE reb, EB_Book * eb, EB_Appendix * appendix, EB_Hookset * text_hookset)
{
    ssize_t len;
    char desc[MAX_STRLEN + 1];
    rb_encoding *enc = REB_TO_RB_ENCODING(reb);

    eb_error = eb_read_text(eb, appendix, text_hookset, (void *) reb,
                            MAX_STRLEN, desc, &len);

    if (len < 0) {
        rb_raise(rb_eRuntimeError, "fail fetching text");
        return Qfalse;
    }
    return rb_external_str_new_with_enc(desc, len, enc);
}

static VALUE
content_fetch_from_pos(VALUE reb, EB_Book * eb, EB_Position * pos, EB_Appendix * appendix, EB_Hookset * text_hookset)
{
    if (eb_seek_text(eb, pos) != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "fail seeking(text)");
        return Qfalse;
    }
    return content_read(reb, eb, appendix, text_hookset);
}

static VALUE
get_item(VALUE reb, EB_Book * eb, EB_Hit * hit)
{
    EB_Hookset *text_hookset;
    EB_Appendix *appendix;
    VALUE item;
    char desc[MAX_STRLEN + 1];
    ssize_t len;
    rb_encoding *enc = REB_TO_RB_ENCODING(reb);
    item = rb_ary_new2(2);

    if (eb_seek_text(eb, &(hit->heading)) < 0) {
        rb_raise(rb_eRuntimeError, "fail seeking");
        return Qfalse;
    }

    text_hookset = get_eb_texthook(reb);
    appendix = get_eb_appendix(reb);

    eb_error = eb_read_heading(eb, appendix, text_hookset, (void *) reb,
                               MAX_STRLEN, desc, &len);
    if (len < 0) {
        rb_raise(rb_eRuntimeError, "fail fetching heading");
        return Qfalse;
    }

    rb_ary_push(item, rb_external_str_new_with_enc(desc, len, enc));
    rb_ary_push(item, content_fetch_from_pos(reb, eb, &(hit->text), appendix, text_hookset));

    return item;
}


static VALUE
hitmaker(VALUE reb, EB_Book * eb, unsigned int max, int flag)
{
    int hitpushed, hitcount;
    EB_Hit hits[MAX_HITS];
    VALUE robj, item, can;
    int i, broken;
    EB_Hookset *text_hookset;

    hitpushed = 0;
    text_hookset = get_eb_texthook(reb);

    robj = rb_ary_new();
    broken = 0;

    while (1) {
        eb_error = eb_hit_list(eb, MAX_HITS, hits, &hitcount);
        if (hitcount == 0)
            break;              /* return */
        if (hitcount < 0) {
            rb_raise(rb_eRuntimeError, "fail getting list");
            return Qfalse;
        }

        for (i = 0; i < hitcount; i++) {
            item = get_item(reb, eb, &hits[i]);
            if (flag == 0) {
                rb_ary_push(robj, item);
            }
            else {
                can = rb_yield(item);
                if (rb_obj_id(can) == rb_obj_id(cEBCancel)) {
                    broken = 1;
                    break;      /* return */
                }
            }
            hitpushed++;
            if (hitpushed >= max) {
                broken = 1;
                break;          /* return */
            }
        }
        if (broken)
            break;
    }

    return (flag == 0) ? robj : INT2NUM(hitpushed);
}

static void
set_keywords(VALUE array, char **buffer, volatile VALUE *gc_guard, rb_encoding *enc)
{
    int i, sz;

    if (TYPE(array) != T_ARRAY) {
        rb_raise(rb_eTypeError, "wordlist must be array of String.");
    }

    sz = RARRAY_LEN(array);
    if (sz > MAX_KEYWORDS) {
        rb_raise(rb_eRuntimeError, "too many keywords(%d).", sz);
    }
    for (i = 0; i < sz; i++) {
        gc_guard[i] = rb_str_export_to_enc(rb_ary_entry(array, i), enc);
        buffer[i] = RSTRING_PTR(gc_guard[i]);
    }
    buffer[sz] = NULL;
}

static VALUE
easy_search(int argc, VALUE * argv, VALUE obj, int wordtype,
            EB_Error_Code(*funct) ())
{
    EB_Book *eb;
    void *word;
    char *buffer[MAX_KEYWORDS + 1];
    int max;
    int r;
    rb_encoding *enc = REB_TO_RB_ENCODING(obj);
    volatile VALUE gc_guard[MAX_KEYWORDS];

    if (argc < 1) {
        rb_raise(rb_eArgError, "missing searchstring");
        return Qfalse;
    }

    if (wordtype == SEARCHTYPE_WORD) {
        VALUE str = rb_str_export_to_enc(argv[0], enc);
        word = RSTRING_PTR(str);
    }
    else {
        set_keywords(argv[0], buffer, gc_guard, enc);
        word = buffer;
    }
    max = (argc > 1) ? NUM2INT(argv[1]) : -1;

    Data_Get_Struct(obj, EB_Book, eb);

    r = (*funct) (eb, word);

    if (r == -1) {
        rb_raise(rb_eRuntimeError, "fail searching");
        return Qfalse;
    }
    /* tricky max: signed<->unsigned -1 */
    return hitmaker(obj, eb, (unsigned) max, (rb_iterator_p())? 1 : 0);
}


static VALUE
reb_searchword(int argc, VALUE * argv, VALUE obj)
{
    return easy_search(argc, argv, obj, SEARCHTYPE_WORD, eb_search_word);
}

static VALUE
reb_exactsearchword(int argc, VALUE * argv, VALUE obj)
{
    return easy_search(argc, argv, obj, SEARCHTYPE_WORD, eb_search_exactword);
}
static VALUE
reb_endsearchword(int argc, VALUE * argv, VALUE obj)
{
    return easy_search(argc, argv, obj, SEARCHTYPE_WORD, eb_search_endword);
}

static VALUE
reb_searchkeyword(int argc, VALUE * argv, VALUE obj)
{
    return easy_search(argc, argv, obj, SEARCHTYPE_WORDLIST, eb_search_keyword);
}


/*   Thanks for Kuroda-san  */
static VALUE
hitmaker2(VALUE reb, EB_Book * eb, unsigned int max, int flag)
{
    int hitcount, i, broken;
    ssize_t len;
    int hitpushed;
    VALUE robj, item, can;
    EB_Hit hits[MAX_HITS];
    char *desc;
    char descbuf1[MAX_STRLEN + 1];
    EB_Position *ebpos;
    char descbuf2[MAX_STRLEN + 1];
    char *prevdesc;
    int prevpage, prevoffset;
    rb_encoding *enc = REB_TO_RB_ENCODING(reb);
    desc = descbuf1;

/*** this 2 lines necessary? (2/4) eblook do like this ***/
    prevdesc = descbuf2;
    *prevdesc = '\0';
    prevpage = 0;
    prevoffset = 0;

    robj = rb_ary_new();

    hitpushed = 0;
    broken = 0;
    while (1) {
        eb_error = eb_hit_list(eb, MAX_HITS, hits, &hitcount);
        if (hitcount == 0)
            break;              /* return */
        if (hitcount < 0) {
            rb_raise(rb_eRuntimeError, "fail getting list");
            return Qfalse;
        }

        for (i = 0; i < hitcount; i++) {
            if (eb_seek_text(eb, &(hits[i].heading)) < 0) {
                rb_raise(rb_eRuntimeError, "fail seeking");
                return Qfalse;
            }
            eb_error = eb_read_heading(eb, get_eb_appendix(reb), get_eb_texthook(reb), (void *) reb, MAX_STRLEN, desc, &len);
            if (len < 0) {
                rb_raise(rb_eRuntimeError, "fail fetching heading");
                return Qfalse;
            }

/*** this 4 lines necessary? (3/4) eblook do like this ***/
            if (prevpage == hits[i].text.page &&
                prevoffset == hits[i].text.offset &&
                strcmp(desc, prevdesc) == 0)
                continue;

            item = rb_ary_new2(2);
            rb_ary_push(item, Data_Make_Struct(cEBPosition, EB_Position, 0, free, ebpos));
            rb_ary_push(item, rb_external_str_new_with_enc(desc, len, enc));
            ebpos->page = hits[i].text.page;
            ebpos->offset = hits[i].text.offset;

            if (flag == 0) {    /* non-iterator */
                rb_ary_push(robj, item);
            }
            else {              /* iterator */
                can = rb_yield(item);
                if (rb_obj_id(can) == rb_obj_id(cEBCancel)) {
                    broken = 1;
                    break;      /* return */
                }
            }
            hitpushed++;
            if (hitpushed >= max) {
                broken = 1;
                break;
            }

/*** this 6 lines necessary? (4/4) eblook do like this ***/
            if (desc == descbuf1) {
                desc = descbuf2;
                prevdesc = descbuf1;
            }
            else {
                desc = descbuf1;
                prevdesc = descbuf2;
            }
            prevpage = hits[i].text.page;
            prevoffset = hits[i].text.offset;
        }
        if (broken)
            break;
    }
    return (flag == 0) ? robj : INT2NUM(hitpushed);
}

static VALUE
position_search(int argc, VALUE * argv, VALUE obj, int wordtype,
                EB_Error_Code(*funct) ())
{
    EB_Book *eb;
    char *buffer[MAX_KEYWORDS];
    void *word;
    int max;
    int r;
    rb_encoding *enc = REB_TO_RB_ENCODING(obj);
    volatile VALUE gc_guard[MAX_KEYWORDS];

    if (argc < 1) {
        rb_raise(rb_eArgError, "missing searchstring");
        return Qfalse;
    }

    if (wordtype == SEARCHTYPE_WORD) {
        VALUE str = rb_str_export_to_enc(argv[0], enc);
        word = RSTRING_PTR(str);
    }
    else {
        set_keywords(argv[0], buffer, gc_guard, enc);
        word = buffer;
    }
    max = (argc > 1) ? NUM2INT(argv[1]) : -1;

    Data_Get_Struct(obj, EB_Book, eb);
    r = (*funct) (eb, word);

    if (r == -1) {
        rb_raise(rb_eRuntimeError, "fail searching");
        return Qfalse;
    }
    return hitmaker2(obj, eb, (unsigned) max, (rb_iterator_p())? 1 : 0);
}

static VALUE
reb_exactsearchword2(int argc, VALUE * argv, VALUE obj)
{
    return position_search(argc, argv, obj, SEARCHTYPE_WORD, eb_search_exactword);
}

static VALUE
reb_searchword2(int argc, VALUE * argv, VALUE obj)
{
    return position_search(argc, argv, obj, SEARCHTYPE_WORD, eb_search_word);
}
static VALUE
reb_endsearchword2(int argc, VALUE * argv, VALUE obj)
{
    return position_search(argc, argv, obj, SEARCHTYPE_WORD, eb_search_endword);
}
static VALUE
reb_searchkeyword2(int argc, VALUE * argv, VALUE obj)
{
    return position_search(argc, argv, obj, SEARCHTYPE_WORDLIST, eb_search_keyword);
}


static VALUE
reb_content(VALUE obj, VALUE position)
{
    EB_Position *ppos;
    EB_Book *eb;
    EB_Appendix *apx;
    EB_Hookset *thook;
    int dlen;
    VALUE robj;

    Data_Get_Struct(obj, EB_Book, eb);
    Data_Get_Struct(position, EB_Position, ppos);
    apx = get_eb_appendix(obj);
    thook = get_eb_texthook(obj);

    robj = content_fetch_from_pos(obj, eb, ppos, apx, thook);

    if (rb_iterator_p()) {
        do {
            rb_yield(robj);
            robj = content_read(obj, eb, apx, thook);
            dlen = MAX_STRLEN - RSTRING_LEN(robj);
        } while (dlen == 0);
    }
    return robj;
}

static VALUE
reb_content_noseek(VALUE obj)
{
    EB_Book *eb;

    Data_Get_Struct(obj, EB_Book, eb);
    return content_read(obj, eb, get_eb_appendix(obj), get_eb_texthook(obj));
}

static VALUE
reb_sethookset(VALUE obj, VALUE hkset)
{
    if (rb_funcall(hkset, rb_intern("is_a?"), 1, cEBHook) != Qtrue && hkset != Qnil) {
        rb_raise(rb_eArgError, "hookset must be nil or an instance of Hookset");
        return Qfalse;
    }
    return rb_iv_set(obj, HOOKSET_EB_IVAR, hkset);
};

static VALUE
reb_gethookset(VALUE obj)
{
    return rb_iv_get(obj, HOOKSET_EB_IVAR);
}

static VALUE
reb_havecopyright(VALUE obj)
{
    EB_Book *eb;
    int r;

    Data_Get_Struct(obj, EB_Book, eb);
    r = eb_have_copyright(eb);
    if (r)
        return Qtrue;
    return Qfalse;
}

static VALUE
reb_copyright(VALUE obj)
{
    EB_Book *eb;
    EB_Position pos;
    EB_Error_Code err;

    Data_Get_Struct(obj, EB_Book, eb);

    err = eb_copyright(eb, &pos);
    if (err == EB_ERR_NO_SUCH_SEARCH) {
        return Qnil;
    }
    else if (err != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "searching copyright was failed.");
        return Qfalse;
    }

    return content_fetch_from_pos(obj, eb, &pos,
                                get_eb_appendix(obj), get_eb_texthook(obj));
}

/* FONT */
static VALUE
reb_font_list(VALUE obj)
{
    EB_Font_Code font_list[EB_MAX_FONTS];
    EB_Book *eb;
    VALUE robj;
    int font_count;
    int i;

    Data_Get_Struct(obj, EB_Book, eb);

    if (eb_font_list(eb, font_list, &font_count) != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "eb_font_list failed.\n");
        return Qfalse;
    }
    robj = rb_ary_new2(font_count);
    for (i = 0; i < font_count; i++) {
        rb_ary_push(robj, INT2FIX(font_list[i]));
    }
    return robj;
}

static EB_Font_Code
get_fontcode(EB_Book * eb)
{
    EB_Font_Code r;
    if (eb_font(eb, &r) != EB_SUCCESS) {
        return -1;
    }
    return r;
}

static VALUE
reb_get_fontheight(VALUE obj)
{
    EB_Book *eb;
    EB_Font_Code r;
    Data_Get_Struct(obj, EB_Book, eb);
    r = get_fontcode(eb);
    return INT2NUM(r);
}

static VALUE
reb_set_fontheight(VALUE obj, VALUE height)
{
    EB_Book *eb;
    Data_Get_Struct(obj, EB_Book, eb);
    if (eb_set_font(eb, NUM2UINT(height)) != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "font set failed.");
        return Qfalse;
    }
    return height;
}

static VALUE
reb_font(VALUE obj, VALUE code, int wideflag, EB_Error_Code(*funct) (EB_Book *, int, char *))
{
    EB_Book *eb;
    VALUE robj;
    struct ExtFont *font;

    Data_Get_Struct(obj, EB_Book, eb);
    robj = Data_Make_Struct(cEBExtFont, struct ExtFont, 0, free, font);
    font->code = NUM2UINT(code);
    font->wideflag = wideflag;
    font->fontsize = get_fontcode(eb);
    if ((*funct) (eb, NUM2UINT(code), font->bitmap) != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "font[%x] bitmap retrieve failed.", NUM2UINT(code));
        return Qfalse;
    }
    return robj;
}

static VALUE
reb_widefont(VALUE obj, VALUE code)
{
    return reb_font(obj, code, 1, eb_wide_font_character_bitmap);
}

static VALUE
reb_narrowfont(VALUE obj, VALUE code)
{
    return reb_font(obj, code, 0, eb_narrow_font_character_bitmap);
}

static VALUE
reb_widestart(VALUE obj)
{
    int code;
    EB_Book *eb;
    Data_Get_Struct(obj, EB_Book, eb);
    if (eb_wide_font_start(eb, &code) != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "subbook not selected or extfont not used in subbook");
        return Qfalse;
    }
    return UINT2NUM(code);
}
static VALUE
reb_wideend(VALUE obj)
{
    int code;
    EB_Book *eb;
    Data_Get_Struct(obj, EB_Book, eb);
    if (eb_wide_font_end(eb, &code) != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "subbook not selected or extfont not used in subbook");
        return Qfalse;
    }
    return UINT2NUM(code);
}
static VALUE
reb_narrowstart(VALUE obj)
{
    int code;
    EB_Book *eb;
    Data_Get_Struct(obj, EB_Book, eb);
    if (eb_narrow_font_start(eb, &code) != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "subbook not selected or extfont not used in subbook");
        return Qfalse;
    }
    return UINT2NUM(code);
}
static VALUE
reb_narrowend(VALUE obj)
{
    int code;
    EB_Book *eb;
    Data_Get_Struct(obj, EB_Book, eb);
    if (eb_narrow_font_end(eb, &code) != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "subbook not selected or extfont not used in subbook");
        return Qfalse;
    }
    return UINT2NUM(code);
}

static VALUE
read_binary(EB_Book * eb, long maxlen, int iterateflag)
{
    char buffer[MAX_STRLEN];
    long readbytes;
    ssize_t bitmap_len;
    int blocksize;
    EB_Error_Code retcode;
    VALUE robj;

    robj = rb_str_new2("");
    if (MAX_STRLEN < maxlen || maxlen < 0) {
        blocksize = MAX_STRLEN;
    }
    else {
        blocksize = maxlen;
    }
    bitmap_len = 1;             /* set non-zero */
    readbytes = 0;


    while (bitmap_len != 0) {
        retcode = eb_read_binary(eb, blocksize, buffer, &bitmap_len);
        if (retcode != EB_SUCCESS) {
            rb_raise(rb_eRuntimeError, "%s", eb_error_message(retcode));
            return Qfalse;
        }
        if (iterateflag) {
            rb_yield(rb_str_new(buffer, bitmap_len));
            readbytes += bitmap_len;
        }
        else {
            rb_str_cat(robj, buffer, bitmap_len);
            readbytes += bitmap_len;
            if (maxlen > 0 && readbytes >= maxlen)
                break;
        }
    }
    rb_obj_taint(robj);

    return iterateflag ? INT2NUM(readbytes) : robj;
}

static VALUE
reb_read_monographic(VALUE obj, VALUE pos, VALUE width, VALUE height)
{
    EB_Error_Code retcode;
    EB_Book *eb;
    EB_Position *epos;

    Data_Get_Struct(obj, EB_Book, eb);
    Data_Get_Struct(pos, EB_Position, epos);

    retcode = eb_set_binary_mono_graphic(eb, epos, NUM2UINT(width), NUM2UINT(height));
    if (retcode != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "failed to set binary mode [monographic]");
        return Qfalse;
    }
    return read_binary(eb, -1, 0);
}

static VALUE
reb_read_colorgraphic(int argc, VALUE * argv, VALUE obj)
{
    EB_Error_Code retcode;
    EB_Book *eb;
    EB_Position *epos;
    long maxlen;

    if (argv == 0) {
        rb_raise(rb_eArgError, "wrong # of arguments(0 for 1 or 2)");
        return Qfalse;
    }

    Data_Get_Struct(obj, EB_Book, eb);
    Data_Get_Struct(argv[0], EB_Position, epos);
    maxlen = (argc > 1) ? NUM2UINT(argv[1]) : MAX_STRLEN;

    retcode = eb_set_binary_color_graphic(eb, epos);
    if (retcode != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "failed to set binary mode [colorgraphic]");
        return Qfalse;
    }
    return read_binary(eb, maxlen, (rb_iterator_p()? 1 : 0));
}

static VALUE
reb_read_wavedata(int argc, VALUE * argv, VALUE obj)
{
    EB_Error_Code retcode;
    EB_Book *eb;
    EB_Position *spos, *epos;
    long maxlen;

    if (argc < 2) {
        rb_raise(rb_eArgError, "both start_pos and end_pos needed.(argument shortage)");
        return Qfalse;
    }
    maxlen = (argc > 2) ? NUM2UINT(argv[2]) : MAX_STRLEN;

    Data_Get_Struct(obj, EB_Book, eb);
    Data_Get_Struct(argv[0], EB_Position, spos);
    Data_Get_Struct(argv[1], EB_Position, epos);

    retcode = eb_set_binary_wave(eb, spos, epos);
    if (retcode != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "failed to set binary mode [wave]");
        return Qfalse;
    }

    return read_binary(eb, maxlen, (rb_iterator_p()? 1 : 0));
}

static VALUE
reb_read_mpeg(int argc, VALUE * argv, VALUE obj)
{
    EB_Error_Code retcode;
    EB_Book *eb;
    long maxlen;
    unsigned int param[4];
    int i;

    if (argc < 4) {
        rb_raise(rb_eArgError, "need code1,code2,code3,code4.");
        return Qnil;
    }
    for (i = 0; i < 4; i++)
        param[i] = NUM2UINT(argv[i]);
    maxlen = (argc > 4) ? NUM2UINT(argv[4]) : MAX_STRLEN;

    Data_Get_Struct(obj, EB_Book, eb);

    retcode = eb_set_binary_mpeg(eb, param);
    if (retcode != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "failed to set binary mode [mpeg]");
        return Qfalse;
    }
    return read_binary(eb, maxlen, (rb_iterator_p()? 1 : 0));
}

static VALUE
reb_compose_mpegfilename(int argc, VALUE * argv, VALUE obj)
{
    EB_Error_Code retcode;
    char buffer[1024];
    unsigned int param[4];
    int i;
    if (argc != 4) {
        rb_raise(rb_eArgError, "4 args needed.(code1-code4)");
        return Qfalse;
    }
    for (i = 0; i < 4; i++)
        param[i] = NUM2UINT(argv[i]);

    retcode = eb_compose_movie_file_name(param, buffer);
    if (retcode != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "failed to compose movie filename.");
        return Qfalse;
    }
    return rb_str_new2(buffer);
}

static VALUE
reb_havemenu(VALUE obj)
{
    EB_Book *eb;
    int r;

    Data_Get_Struct(obj, EB_Book, eb);
    r = eb_have_menu(eb);
    if (r)
        return Qtrue;
    return Qfalse;
}

static VALUE
reb_menu(VALUE obj)
{
    EB_Book *eb;
    EB_Position pos;
    EB_Error_Code err;

    Data_Get_Struct(obj, EB_Book, eb);

    err = eb_menu(eb, &pos);
    if (err == EB_ERR_NO_SUCH_SEARCH) {
        return Qnil;
    }
    else if (err != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "%s", eb_error_message(err));
        return Qfalse;
    }
    return content_fetch_from_pos(obj, eb, &pos,
                                get_eb_appendix(obj), get_eb_texthook(obj));
}


static VALUE
reb_menu2(VALUE obj)
{
    EB_Book *eb;
    EB_Position pos, *rpos;
    EB_Error_Code err;
    VALUE robj;

    Data_Get_Struct(obj, EB_Book, eb);

    err = eb_menu(eb, &pos);
    if (err == EB_ERR_NO_SUCH_SEARCH) {
        return Qnil;
    }
    else if (err != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "failed to fetch menu(%d)\n", err);
        return Qfalse;
    }
    robj = Data_Make_Struct(cEBPosition, EB_Position, 0, free, rpos);
    rpos->page = pos.page;
    rpos->offset = pos.offset;
    return robj;
}

static VALUE
reb_appendixpath(VALUE obj, VALUE path)
{
    EB_Appendix *appendix;
    appendix = get_eb_appendix(obj);
    if (path != Qnil) {
        eb_bind_appendix(appendix, StringValueCStr(path));
    }
    else {
        eb_finalize_appendix(appendix);
        eb_initialize_appendix(appendix);
    }
    return path;
}

/**
*  ExtFont
**/

static VALUE
rebfont_wide_p(VALUE obj)
{
    struct ExtFont *font;
    Data_Get_Struct(obj, struct ExtFont, font);
    if (font->wideflag)
        return Qtrue;
    return Qfalse;
}

static VALUE
rebfont_code(VALUE obj)
{
    struct ExtFont *font;
    Data_Get_Struct(obj, struct ExtFont, font);
    return UINT2NUM(font->code);
}

static VALUE
font2bitmapformat(struct ExtFont *font,
                  EB_Error_Code(*w_size_func) (),
                  EB_Error_Code(*n_size_func) (),
#if EB_VERSION_MAJOR < 4 || (EB_VERSION_MAJOR == 4 && EB_VERSION_MINOR < 1)
                  void (*conv_func) ())
#else
                  EB_Error_Code(*conv_func) ())
#endif
{
    int size, retcode;
    int height, width;
    char *buffer;
    VALUE robj;

#if HAVE_XBMSIZE_BUG            /* eb_???_font_xbm_size() doesn't work on eb-3.2.3 */
    if (font->wideflag == 1) {
        retcode = (*w_size_func) (font->fontsize, &size);
    }
    else {
        retcode = (*n_size_func) (font->fontsize, &size);
    }
    if (size == 0) {
        rb_raise(rb_eRuntimeError, "conversion size error[return code%d]", retcode);
        return Qfalse;
    }
#else /* quick and loose hack */
    size = 0xffff;
#endif
    buffer = malloc(size + 1);
    if (buffer == NULL) {
        rb_raise(rb_eRuntimeError, "malloc error");
        return Qfalse;
    }

    if (font->wideflag == 1) {
        retcode = eb_wide_font_width2(font->fontsize, &width);
    }
    else {
        retcode = eb_narrow_font_width2(font->fontsize, &width);
    }
    if (retcode != EB_SUCCESS ||
        eb_font_height2(font->fontsize, &height) != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "fontsize unknown.");
        return Qfalse;
    };

    (*conv_func) (font->bitmap, width, height, buffer, &size);
    robj = rb_tainted_str_new(buffer, size);
    free(buffer);
    return robj;
}

static VALUE
rebfont_toxbm(VALUE extfont)
{
    struct ExtFont *font;
    Data_Get_Struct(extfont, struct ExtFont, font);
    return font2bitmapformat(font, eb_wide_font_xbm_size,
                             eb_narrow_font_xbm_size,
                             eb_bitmap_to_xbm);
}

static VALUE
rebfont_toxpm(VALUE extfont)
{
    struct ExtFont *font;
    Data_Get_Struct(extfont, struct ExtFont, font);
    return font2bitmapformat(font, eb_wide_font_xpm_size,
                             eb_narrow_font_xpm_size,
                             eb_bitmap_to_xpm);
}

static VALUE
rebfont_togif(VALUE extfont)
{
    struct ExtFont *font;
    Data_Get_Struct(extfont, struct ExtFont, font);
    return font2bitmapformat(font, eb_wide_font_gif_size,
                             eb_narrow_font_gif_size,
                             eb_bitmap_to_gif);
}

static VALUE
rebfont_tobmp(VALUE extfont)
{
    struct ExtFont *font;
    Data_Get_Struct(extfont, struct ExtFont, font);
    return font2bitmapformat(font, eb_wide_font_bmp_size,
                             eb_narrow_font_bmp_size,
                             eb_bitmap_to_bmp);
}

#if HAVE_EB_BITMAP_TO_PNG
static VALUE
rebfont_topng(VALUE extfont)
{
    struct ExtFont *font;
    Data_Get_Struct(extfont, struct ExtFont, font);
    return font2bitmapformat(font, eb_wide_font_png_size,
                             eb_narrow_font_png_size,
                             eb_bitmap_to_png);
}
#endif

/**
*  EBPosition
**/
static VALUE
reb_pos_initialize(int argc, VALUE * argv, VALUE klass)
{
    VALUE robj, page, offset;
    EB_Position *ebpos;

    robj = Data_Make_Struct(klass, EB_Position, 0, free, ebpos);
    if (rb_scan_args(argc, argv, "02", &page, &offset) > 0) {
        Check_Type(page, T_FIXNUM);
        Check_Type(offset, T_FIXNUM);
        ebpos->page = FIX2INT(page);
        ebpos->offset = FIX2INT(offset);
    }
    return robj;
}

static VALUE
reb_pos_get_page(VALUE obj)
{
    EB_Position *pos;

    Data_Get_Struct(obj, EB_Position, pos);

    return INT2FIX(pos->page);
}

static VALUE
reb_pos_get_offset(VALUE obj)
{
    EB_Position *pos;

    Data_Get_Struct(obj, EB_Position, pos);

    return INT2FIX(pos->offset);
}

static VALUE
reb_pos_set_page(VALUE obj, VALUE spage)
{
    EB_Position *pos;

    Data_Get_Struct(obj, EB_Position, pos);
    pos->page = FIX2INT(spage);

    return obj;
}

static VALUE
reb_pos_set_offset(VALUE obj, VALUE soffset)
{
    EB_Position *pos;

    Data_Get_Struct(obj, EB_Position, pos);
    pos->offset = FIX2INT(soffset);

    return obj;
}

static VALUE
rebhk_new(VALUE klass)
{
    VALUE robj;
    EB_Hookset *text_hookset;

    robj = Data_Make_Struct(cEBHook, EB_Hookset, 0, finalize_hookset, text_hookset);
    eb_initialize_hookset(text_hookset);
    rb_iv_set(robj, HOOKSET_PROCS_IVAR, rb_ary_new2(EB_NUMBER_OF_HOOKS));
    return robj;
}

static VALUE
rebhk_register(int argc, VALUE * argv, VALUE self)
{
    VALUE proc = Qnil;
    int hook_type;
    EB_Hookset *text_hookset;
    EB_Hook hook;

    switch (argc) {
    case 1:
#if HAVE_RB_BLOCK_PROC
        proc = rb_block_proc();
#else   
        proc = rb_f_lambda();
#endif
        break;
    case 2:
        proc = argv[1];
        break;
    default:
        rb_raise(rb_eArgError, "wrong # of arguments");
        break;
    }

    hook_type = FIX2UINT(argv[0]);
    rb_ary_store(rb_iv_get(self, HOOKSET_PROCS_IVAR), hook_type, proc);
    Data_Get_Struct(self, EB_Hookset, text_hookset);
    hook.code = hook_type;
    if (proc != Qnil) {
        hook.function = (int (*)()) (text_hook);
    }
    else {
        hook.function = NULL;
    }

    if (eb_set_hook(text_hookset, &hook) != EB_SUCCESS) {
        rb_raise(rb_eRuntimeError, "set_hook(%d) failed", hook_type);
        return Qfalse;
    }
    return Qnil;
}

static VALUE
reb_mod_initialize(VALUE obj)
{
    eb_initialize_library();
    return obj;
}

static VALUE
reb_mod_finalize(VALUE obj)
{
/* printf("mod_finalize\n"); */
    eb_finalize_library();
    return obj;
}

static VALUE
reb_dontuseexception(VALUE obj)
{
    rb_raise(rb_eNameError, "Don't use this method.");
    return Qfalse;
}

static void
define_constants_under(VALUE mod)
{
    rb_define_const(mod, "HOOK_INITIALIZE", INT2FIX(EB_HOOK_INITIALIZE));
    rb_define_const(mod, "HOOK_BEGIN_NARROW", INT2FIX(EB_HOOK_BEGIN_NARROW));
    rb_define_const(mod, "HOOK_END_NARROW", INT2FIX(EB_HOOK_END_NARROW));
    rb_define_const(mod, "HOOK_BEGIN_SUBSCRIPT", INT2FIX(EB_HOOK_BEGIN_SUBSCRIPT));
    rb_define_const(mod, "HOOK_END_SUBSCRIPT", INT2FIX(EB_HOOK_END_SUBSCRIPT));
    rb_define_const(mod, "HOOK_SET_INDENT", INT2FIX(EB_HOOK_SET_INDENT));
    rb_define_const(mod, "HOOK_NEWLINE", INT2FIX(EB_HOOK_NEWLINE));
    rb_define_const(mod, "HOOK_BEGIN_SUPERSCRIPT", INT2FIX(EB_HOOK_BEGIN_SUPERSCRIPT));
    rb_define_const(mod, "HOOK_END_SUPERSCRIPT", INT2FIX(EB_HOOK_END_SUPERSCRIPT));
    rb_define_const(mod, "HOOK_BEGIN_NO_NEWLINE", INT2FIX(EB_HOOK_BEGIN_NO_NEWLINE));
    rb_define_const(mod, "HOOK_END_NO_NEWLINE", INT2FIX(EB_HOOK_END_NO_NEWLINE));
    rb_define_const(mod, "HOOK_BEGIN_EMPHASIS", INT2FIX(EB_HOOK_BEGIN_EMPHASIS));
    rb_define_const(mod, "HOOK_END_EMPHASIS", INT2FIX(EB_HOOK_END_EMPHASIS));
    rb_define_const(mod, "HOOK_BEGIN_CANDIDATE", INT2FIX(EB_HOOK_BEGIN_CANDIDATE));
    rb_define_const(mod, "HOOK_END_CANDIDATE_GROUP", INT2FIX(EB_HOOK_END_CANDIDATE_GROUP));
    rb_define_const(mod, "HOOK_END_CANDIDATE_LEAF", INT2FIX(EB_HOOK_END_CANDIDATE_LEAF));
    rb_define_const(mod, "HOOK_BEGIN_REFERENCE", INT2FIX(EB_HOOK_BEGIN_REFERENCE));
    rb_define_const(mod, "HOOK_END_REFERENCE", INT2FIX(EB_HOOK_END_REFERENCE));
    rb_define_const(mod, "HOOK_BEGIN_KEYWORD", INT2FIX(EB_HOOK_BEGIN_KEYWORD));
    rb_define_const(mod, "HOOK_END_KEYWORD", INT2FIX(EB_HOOK_END_KEYWORD));
    rb_define_const(mod, "HOOK_NARROW_FONT", INT2FIX(EB_HOOK_NARROW_FONT));
    rb_define_const(mod, "HOOK_WIDE_FONT", INT2FIX(EB_HOOK_WIDE_FONT));
    rb_define_const(mod, "HOOK_ISO8859_1", INT2FIX(EB_HOOK_ISO8859_1));
    rb_define_const(mod, "HOOK_NARROW_JISX0208", INT2FIX(EB_HOOK_NARROW_JISX0208));
    rb_define_const(mod, "HOOK_WIDE_JISX0208", INT2FIX(EB_HOOK_WIDE_JISX0208));
    rb_define_const(mod, "HOOK_GB2312", INT2FIX(EB_HOOK_GB2312));

    rb_define_const(mod, "HOOK_BEGIN_MONO_GRAPHIC", INT2FIX(EB_HOOK_BEGIN_MONO_GRAPHIC));
    rb_define_const(mod, "HOOK_END_MONO_GRAPHIC", INT2FIX(EB_HOOK_END_MONO_GRAPHIC));

    rb_define_const(mod, "HOOK_BEGIN_GRAY_GRAPHIC", INT2FIX(EB_HOOK_BEGIN_GRAY_GRAPHIC));
    rb_define_const(mod, "HOOK_END_GRAY_GRAPHIC", INT2FIX(EB_HOOK_END_GRAY_GRAPHIC));

    rb_define_const(mod, "HOOK_BEGIN_COLOR_BMP", INT2FIX(EB_HOOK_BEGIN_COLOR_BMP));
    rb_define_const(mod, "HOOK_BEGIN_COLOR_JPEG", INT2FIX(EB_HOOK_BEGIN_COLOR_JPEG));
    rb_define_const(mod, "HOOK_END_COLOR_GRAPHIC", INT2FIX(EB_HOOK_END_COLOR_GRAPHIC));
    rb_define_const(mod, "HOOK_END_IN_COLOR_GRAPHIC", INT2FIX(EB_HOOK_END_IN_COLOR_GRAPHIC));

    rb_define_const(mod, "HOOK_BEGIN_GRAPHIC_REFERENCE", INT2FIX(EB_HOOK_BEGIN_GRAPHIC_REFERENCE));
    rb_define_const(mod, "HOOK_END_GRAPHIC_REFERENCE", INT2FIX(EB_HOOK_END_GRAPHIC_REFERENCE));
    rb_define_const(mod, "HOOK_GRAPHIC_REFERENCE", INT2FIX(EB_HOOK_GRAPHIC_REFERENCE));

#if !(EB_VERSION_MAJOR == 3 && EB_VERSION_MINOR == 2)
    rb_define_const(mod, "HOOK_BEGIN_IN_COLOR_BMP", INT2FIX(EB_HOOK_BEGIN_IN_COLOR_BMP));
    rb_define_const(mod, "HOOK_BEGIN_IN_COLOR_JPEG", INT2FIX(EB_HOOK_BEGIN_IN_COLOR_JPEG));
#endif

    rb_define_const(mod, "HOOK_BEGIN_WAVE", INT2FIX(EB_HOOK_BEGIN_WAVE));
    rb_define_const(mod, "HOOK_END_WAVE", INT2FIX(EB_HOOK_END_WAVE));
    rb_define_const(mod, "HOOK_BEGIN_MPEG", INT2FIX(EB_HOOK_BEGIN_MPEG));
    rb_define_const(mod, "HOOK_END_MPEG", INT2FIX(EB_HOOK_END_MPEG));
    rb_define_const(mod, "HOOK_BEGIN_DECORATION", INT2FIX(EB_HOOK_BEGIN_DECORATION));
    rb_define_const(mod, "HOOK_END_DECORATION", INT2FIX(EB_HOOK_END_DECORATION));

    rb_define_const(mod, "FONT_16", INT2FIX(EB_FONT_16));
    rb_define_const(mod, "FONT_24", INT2FIX(EB_FONT_24));
    rb_define_const(mod, "FONT_30", INT2FIX(EB_FONT_30));
    rb_define_const(mod, "FONT_48", INT2FIX(EB_FONT_48));
    rb_define_const(mod, "FONT_INVALID", INT2FIX(EB_FONT_INVALID));
}


/******************************
**   Init
*/

void
Init_eb()
{
#ifdef HAVE_EB_PTHREAD_ENABLED
#ifdef RUBY_EB_ENABLE_PTHREAD
    if (!eb_pthread_enabled()) {
       rb_raise(rb_eRuntimeError, "The RubyEB is compiled for pthread-enabled EB library.");
     }
#else
    if (eb_pthread_enabled()) {
       rb_raise(rb_eRuntimeError, "The RubyEB is compiled for pthread-disabled EB library.");
     }
#endif
#endif
    id_call = rb_intern("call");
    sym_eb_encidx = ID2SYM(rb_intern("@__ruby_eb_encidx__"));

    mEB = rb_define_module("EB");
    rb_define_const(mEB,"RUBYEB_VERSION",rb_str_new2(RUBYEB_VERSION));
    cEBook = rb_define_class_under(mEB, "Book", rb_cObject);
    cEBCancel = rb_define_class_under(mEB, "Cancel", rb_cObject);
    cEBPosition = rb_define_class_under(mEB, "Position", rb_cObject);
    cEBExtFont = rb_define_class_under(mEB, "ExtFont", rb_cObject);
    cEBHook = rb_define_class_under(mEB, "Hookset", rb_cObject);
    cEBAppendix = rb_define_class_under(mEB, "Appendix", rb_cObject);

    rb_define_singleton_method(mEB, "errorcode", rEB_error, 0);
    rb_define_singleton_method(mEB, "error_message", rEB_errormsg, 0);

    rb_define_singleton_method(cEBook, "new", reb_initialize, 0);
    rb_define_method(cEBook, "bind", reb_bind, 1);
    rb_define_method(cEBook, "disctype", reb_disktype, 0);
    rb_define_method(cEBook, "disktype", reb_disktype, 0);
    rb_define_method(cEBook, "suspend", reb_suspend, 0);
    rb_define_method(cEBook, "bound?", reb_isbound, 0);
    rb_define_method(cEBook, "path", reb_path, 0);
    rb_define_method(cEBook, "charcode", reb_charcode, 0);

    rb_define_method(cEBook, "subbook_count", reb_subbookcount, 0);
    rb_define_method(cEBook, "subbook_list", reb_subbooklist, 0);
    rb_define_method(cEBook, "title", reb_subbooktitle, -1);
    rb_define_method(cEBook, "directory", reb_subbookdirectory, -1);

    rb_define_method(cEBook, "set", reb_setsubbook, 1);
    rb_define_method(cEBook, "subbook=", reb_setsubbook, 1);
    rb_define_method(cEBook, "subbook", reb_getsubbook, 0);
    rb_define_method(cEBook, "unset", reb_unsetsubbook, 0);

    rb_define_method(cEBook, "search", reb_searchword, -1);
    rb_define_method(cEBook, "exactsearch", reb_exactsearchword, -1);
    rb_define_method(cEBook, "endsearch", reb_endsearchword, -1);
    rb_define_method(cEBook, "keywordsearch", reb_searchkeyword, -1);

    rb_define_method(cEBook, "search2", reb_searchword2, -1);
    rb_define_method(cEBook, "exactsearch2", reb_exactsearchword2, -1);
    rb_define_method(cEBook, "endsearch2", reb_endsearchword2, -1);
    rb_define_method(cEBook, "keywordsearch2", reb_searchkeyword2, -1);

    rb_define_method(cEBook, "content", reb_content, 1);
    rb_define_method(cEBook, "content_noseek", reb_content_noseek, 0);

    rb_define_method(cEBook, "search_available?", reb_havewordsearch, 0);
    rb_define_method(cEBook, "exactsearch_available?", reb_haveexactsearch, 0);
    rb_define_method(cEBook, "endsearch_available?", reb_haveendsearch, 0);
    rb_define_method(cEBook, "keywordsearch_available?", reb_havekeywordsearch, 0);

    rb_define_method(cEBook, "hookset=", reb_sethookset, 1);
    rb_define_method(cEBook, "hookset", reb_gethookset, 0);

    rb_define_method(cEBook, "copyright_available?", reb_havecopyright, 0);
    rb_define_method(cEBook, "copyright", reb_copyright, 0);

    rb_define_method(cEBook, "fontcode_list", reb_font_list, 0);
    rb_define_method(cEBook, "get_widefont", reb_widefont, 1);
    rb_define_method(cEBook, "get_narrowfont", reb_narrowfont, 1);
    rb_define_method(cEBook, "fontcode", reb_get_fontheight, 0);
    rb_define_method(cEBook, "fontcode=", reb_set_fontheight, 1);
    rb_define_method(cEBook, "wide_startcode", reb_widestart, 0);
    rb_define_method(cEBook, "wide_endcode", reb_wideend, 0);
    rb_define_method(cEBook, "narrow_startcode", reb_narrowstart, 0);
    rb_define_method(cEBook, "narrow_endcode", reb_narrowend, 0);
    rb_define_method(cEBook, "read_monographic", reb_read_monographic, 3);
    rb_define_method(cEBook, "read_colorgraphic", reb_read_colorgraphic, -1);
    rb_define_method(cEBook, "read_wavedata", reb_read_wavedata, -1);
    rb_define_method(cEBook, "read_mpeg", reb_read_mpeg, -1);
    rb_define_method(cEBook, "compose_mpegfilename", reb_compose_mpegfilename, -1);

    /* menues */
    rb_define_method(cEBook, "menu_available?", reb_havemenu, 0);
    rb_define_method(cEBook, "menu", reb_menu, 0);
    rb_define_method(cEBook, "menu2", reb_menu2, 0);

    /* appendix_path= is temporal now */
    rb_define_method(cEBook, "appendix_path=", reb_appendixpath, 1);

    rb_define_singleton_method(cEBHook, "new", rebhk_new, 0);
    rb_define_method(cEBHook, "register", rebhk_register, -1);

    rb_define_singleton_method(cEBExtFont, "new", reb_dontuseexception, 0);
    rb_define_method(cEBExtFont, "widefont?", rebfont_wide_p, 0);
    rb_define_method(cEBExtFont, "code", rebfont_code, 0);
    rb_define_method(cEBExtFont, "to_xbm", rebfont_toxbm, 0);
    rb_define_method(cEBExtFont, "to_xpm", rebfont_toxpm, 0);
    rb_define_method(cEBExtFont, "to_gif", rebfont_togif, 0);
    rb_define_method(cEBExtFont, "to_bmp", rebfont_tobmp, 0);
#if HAVE_EB_BITMAP_TO_PNG
    rb_define_method(cEBExtFont, "to_png", rebfont_topng, 0);
#endif

    rb_define_singleton_method(cEBPosition, "new", reb_pos_initialize, -1);
    rb_define_method(cEBPosition, "page", reb_pos_get_page, 0);
    rb_define_method(cEBPosition, "offset", reb_pos_get_offset, 0);
    rb_define_method(cEBPosition, "page=", reb_pos_set_page, 1);
    rb_define_method(cEBPosition, "offset=", reb_pos_set_offset, 1);

    eb_initialize_library();
    /* Don't call the following methods manually */
    rb_define_module_function(mEB, "Initialize", reb_mod_initialize, 0);
    rb_define_module_function(mEB, "Finalize", reb_mod_finalize, 0);
    rb_eval_string("at_exit do EB::Finalize(); end\n");

    define_constants_under(mEB);
}
