#include <string.h>
#ifdef sun
#include <alloca.h>
#endif
#include "config.h"
#include "lint.h"
#include "interpret.h"
#include "object.h"
#include "wiz_list.h"
#include "regexp.h"
#include "exec.h"

#if defined(__GNUC__) && !defined(lint)
#define INLINE inline
#else
#define INLINE
#endif

/*
 * This file contains functions used to manipulate arrays.
 * Some of them are connected to efuns, and some are only used internally
 * by the game driver.
 */

extern int d_flag;

int num_arrays;
int total_array_size;

/*
 * Make an empty vector for everyone to use, never to be deallocated.
 * It is cheaper to reuse it, than to use malloc() and allocate.
 */
struct vector null_vector = {
    0,	/* size */
    1	/* Ref count, which will ensure that it will never be deallocated */
};

/*
 * Allocate an array of size 'n'.
 */
struct vector *allocate_array(n)
    int n;
{
    extern struct svalue const0;
    int i;
    struct vector *p;

    if (n < 0 || n > MAX_ARRAY_SIZE)
	error("Illegal array size.\n");
    if (n == 0) {
        p = &null_vector;
	p->ref++;
	return p;
    }
    num_arrays++;
    total_array_size += sizeof (struct vector) + sizeof (struct svalue) *
	(n-1);
    p = ALLOC_VECTOR(n);
    p->ref = 1;
    p->size = n;
    p->user = current_object->user;
    if (p->user)
	p->user->size_array += n;
    for (i=0; i<n; i++)
	p->item[i] = const0;
    return p;
}

void free_vector(p)
    struct vector *p;
{
    int i;
    p->ref--;
    if (p->ref > 0)
	return;
#ifdef DEBUG
    if (p == &null_vector)
	fatal("Tried to free the zero-size shared vector.\n");
#endif
    for (i=0; i<p->size; i++)
	free_svalue(&p->item[i]);
    if (p->user)
	p->user->size_array -= p->size;
    num_arrays--;
    total_array_size -= sizeof (struct vector) + sizeof (struct svalue) *
	(p->size-1);
    free((char *)p);
}

struct vector *shrink_array(p, n)
    struct vector *p;
    int n;
{
    if (n <= p->size >> 1) {
	struct vector *res;

	res = slice_array(p, 0, n-1);
	free_vector(p);
	return res;
    }
    total_array_size += sizeof (struct svalue) * (n - p->size);
    if (p->user)
	p->user->size_array += n - p->size;
    p->size = n;
    return p;
}

struct vector *explode_string(str, del)
    char *str, *del;
{
    char *p, *beg;
    int num, len;
    struct vector *ret;
    char *buff;

    len = strlen(del);
    /*
     * Take care of the case where the delimiter is an
     * empty string. Then, return an array with only one element,
     * which is the original string.
     */
    if (len == 0) {
	ret = allocate_array(1);
	ret->item[0].type = T_STRING;
	ret->item[0].string_type = STRING_MALLOC;
	ret->item[0].u.string = string_copy(str);
	return ret;
    }
    /*
     * Skip leading 'del' strings, if any.
     */
    while(strncmp(str, del, len) == 0) {
	str += len;
	if (str[0] == '\0')
	    return 0; /* Should we return empty array here? /Johan */
    }
    /*
     * Find number of occurences of the delimiter 'del'.
     */
    for (p=str, num=0; *p;) {
	if (strncmp(p, del, len) == 0) {
	    num++;
	    p += len;
	} else
	    p += 1;
    }
    /*
     * Compute number of array items. It is either number of delimiters,
     * or, one more.
     */
    if (strlen(str) < len || strcmp(str + strlen(str) - len, del) != 0)
	num++;
    buff = xalloc(strlen(str) + 1);
    ret = allocate_array(num);
    for (p=str, beg = str, num=0; *p; ) {
	if (strncmp(p, del, len) == 0) {
	    strncpy(buff, beg, p - beg);
	    buff[p-beg] = '\0';
	    if (num >= ret->size)
		fatal("Too big index in explode !\n");
	    /* free_svalue(&ret->item[num]); Not needed for new array */
	    ret->item[num].type = T_STRING;
	    ret->item[num].string_type = STRING_MALLOC;
	    ret->item[num].u.string = string_copy(buff);
	    num++;
	    beg = p + len;
	    p = beg;
	} else {
	    p += 1;
	}
    }
    /* Copy last occurence, if there was not a 'del' at the end. */
    if (*beg != '\0') {
	free_svalue(&ret->item[num]);
	ret->item[num].type = T_STRING;
	ret->item[num].string_type = STRING_MALLOC;
	ret->item[num].u.string = string_copy(beg);
    }
    free(buff);
    return ret;
}

char *implode_string(arr, del)
    struct vector *arr;
    char *del;
{
    int size, i, num;
    char *p, *q;

    for (i=0, size = 0, num = 0; i < arr->size; i++) {
	if (arr->item[i].type == T_STRING) {
	    size += strlen(arr->item[i].u.string);
	    num++;
	}
    }
    if (num == 0)
	return string_copy("");
    p = xalloc(size + (num-1) * strlen(del) + 1);
    q = p;
    p[0] = '\0';
    for (i=0, size=0, num=0; i < arr->size; i++) {
	if (arr->item[i].type == T_STRING) {
	    if (num > 0) {
		strcpy(p, del);
		p += strlen(del);
	    }
	    strcpy(p, arr->item[i].u.string);
	    p += strlen(arr->item[i].u.string);
	    num++;
	}
    }
    return q;
}

struct vector *users() {
    struct object *ob;
    extern int num_player; /* set by comm1.c */
    int i;
    struct vector *ret;

    ret = allocate_array(num_player);
    for (i = 0; i < num_player; i++) {
	ret->item[i].type = T_OBJECT;
	ret->item[i].u.ob = ob = get_interactive_object(i);
	add_ref(ob, "users");
    }
    return ret;
}

/*
 * Check that an assignment to an array item is not cyclic.
 */
static void check_for_recursion(vec, v)
    struct vector *vec, *v;
{
    register int i;
    extern int eval_cost;

    if (vec->user)
	vec->user->cost++;
    eval_cost++;
    if (v == vec)
	error("Recursive asignment of vectors.\n");
    for (i=0; i<v->size; i++) {
	if (v->item[i].type == T_POINTER)
	    check_for_recursion(vec, v->item[i].u.vec);
    }
}

/*
 * Slice of an array.
 */
struct vector *slice_array(p,from,to)
    struct vector *p;
    int from;
    int to;
{
    struct vector *d;
    int cnt;
    
    if (from < 0)
	from = 0;
    if (from >= p->size)
	return allocate_array(0); /* Slice starts above array */
    if (to >= p->size)
	to = p->size-1;
    if (to < from)
	return allocate_array(0); 
    
    d = allocate_array(to-from+1);
    for (cnt=from;cnt<=to;cnt++) 
	assign_svalue_no_free (&d->item[cnt-from], &p->item[cnt]);
    
    return d;
}

/* EFUN: filter_array
   
   Runs all elements of an array through ob->func()
   and returns an array holding those elements that ob->func
   returned 1 for.
   */
struct vector *filter (p, func, ob, extra)
    struct vector *p;
    char *func;
    struct object *ob;
    struct svalue *extra;
{
    struct vector *r;
    struct svalue *v;
    char *flags;
    int cnt,res;
    
    res=0;
    r=0;
    if ( !func || !ob || (ob->flags & O_DESTRUCTED)) {
	if (d_flag) debug_message ("filter: invalid agument\n");
	return 0;
    }
    if (p->size<1)
	return allocate_array(0);

    flags=xalloc(p->size+1); 
    for (cnt=0;cnt<p->size;cnt++) {
	flags[cnt]=0;
	push_svalue(&p->item[cnt]);
	if (extra) {
	    push_svalue(extra);
	    v = apply (func, ob, 2);
	} else {
	    v = apply (func, ob, 1);
	}
	if ((v) && (v->type==T_NUMBER)) {
	    if (v->u.number) { flags[cnt]=1; res++; }
	}
    }
    r = allocate_array(res);
    if (res) {
	for (cnt = res = 0; (res < r->size) && (cnt < p->size); cnt++) {
	    if (flags[cnt]) 
		assign_svalue_no_free (&r->item[res++], &p->item[cnt]);
	}
    }
    free(flags);
    return r;
}

/* Unique maker
   
   These routines takes an array of objects and calls the function 'func'
   in them. The return values are used to decide which of the objects are
   unique. Then an array on the below form are returned:
   
   ({
   ({Same1:1, Same1:2, Same1:3, .... Same1:N }),
   ({Same2:1, Same2:2, Same2:3, .... Same2:N }),
   ({Same3:1, Same3:2, Same3:3, .... Same3:N }),
   ....
   ....
   ({SameM:1, SameM:2, SameM:3, .... SameM:N }),
   })
   i.e an array of arrays consisting of lists of objectpointers
   to all the nonunique objects for each unique set of objects.
   
   The basic purpose of this routine is to speed up the preparing of the
   array used for describing.
   
   */

struct unique
{
    int count;
    struct svalue *val;
    struct svalue mark;
    struct unique *same;
    struct unique *next;
};

static int sameval(arg1,arg2)
    struct svalue *arg1;
    struct svalue *arg2;
{
    if (!arg1 || !arg2) return 0;
    if (arg1->type == T_NUMBER && arg2->type == T_NUMBER) {
	return arg1->u.number == arg2->u.number;
    } else if (arg1->type == T_POINTER && arg2->type == T_POINTER) {
	return arg1->u.vec == arg2->u.vec;
    } else if (arg1->type == T_STRING && arg2->type == T_STRING) {
	return !strcmp(arg1->u.string, arg2->u.string);
    } else if (arg1->type == T_OBJECT && arg2->type == T_OBJECT) {
	return arg1->u.ob == arg2->u.ob;
    } else
	return 0;
}


static int put_in(ulist,marker,elem)
    struct unique **ulist;
    struct svalue *marker;
    struct svalue *elem;
{
    struct unique *llink,*slink,*tlink;
    int cnt,fixed;
    
    llink= *ulist;
    cnt=0; fixed=0;
    while (llink) {
	if ((!fixed) && (sameval(marker,&(llink->mark)))) {
	    for (tlink=llink;tlink->same;tlink=tlink->same) (tlink->count)++;
	    (tlink->count)++;
	    slink = (struct unique *) xalloc(sizeof(struct unique));
	    slink->count = 1;
	    assign_svalue_no_free(&slink->mark,marker);
	    slink->val = elem;
	    slink->same = 0;
	    slink->next = 0;
	    tlink->same = slink;
	    fixed=1; /* We want the size of the list so do not break here */
	}
	llink=llink->next; cnt++;
    }
    if (fixed) return cnt;
    llink = (struct unique *) xalloc(sizeof(struct unique));
    llink->count = 1;
    assign_svalue_no_free(&llink->mark,marker);
    llink->val = elem;
    llink->same = 0;
    llink->next = *ulist;
    *ulist = llink;
    return cnt+1;
}


struct vector *
make_unique(arr,func,skipnum)
    struct vector *arr;
    char *func;
    struct svalue *skipnum;
{
    struct svalue *v;
    struct vector *res,*ret;
    struct unique *head,*nxt,*nxt2;
    
    int cnt,ant,cnt2;
    
    if (arr->size < 1)
	return allocate_array(0);

    head = 0; ant=0; arr->ref++;
    for(cnt=0;cnt<arr->size;cnt++) if (arr->item[cnt].type == T_OBJECT) {
	v = apply(func,arr->item[cnt].u.ob,0);
	if ((!v) || (v->type!=skipnum->type) || !sameval(v,skipnum)) 
	    if (v) ant=put_in(&head,v,&(arr->item[cnt]));
    }
    arr->ref--;
    ret = allocate_array(ant);
    
    for (cnt=ant-1;cnt>=0;cnt--) { /* Reverse to compensate put_in */
	ret->item[cnt].type = T_POINTER;
	ret->item[cnt].u.vec = res = allocate_array(head->count);
	nxt2 = head;
	head = head->next;
	cnt2 = 0;
	while (nxt2) {
	    assign_svalue_no_free (&res->item[cnt2++], nxt2->val);
	    free_svalue(&nxt2->mark);
	    nxt = nxt2->same;
	    free((char *) nxt2);
	    nxt2 = nxt;
	}
	if (!head) 
	    break; /* It shouldn't but, to avoid skydive just in case */
    }
    return ret;
}

/*
 * End of Unique maker
 *************************
 */

/* Concatenation of two arrays into one
 */
struct vector *add_array(p,r)
    struct vector *p, *r;
{
    int cnt,res;
    struct vector *d;
    
    d = allocate_array(p->size+r->size);
    res=0;
    for (cnt=0;cnt<p->size;cnt++) {
	assign_svalue_no_free (&d->item[res],&p->item[cnt]);
	res++; 
    }
    for (cnt=0;cnt<r->size;cnt++) {
	assign_svalue_no_free (&d->item[res],&r->item[cnt]);
	res++; 
    }
    return d;
}

struct vector *subtract_array(minuend, subtrahend)
    struct vector *minuend, *subtrahend;
{
    struct vector *order_alist PROT((struct vector *));
    struct vector *vtmpp;
    static struct vector vtmp = { 1, 1,
#ifdef DEBUG
	1,
#endif
	0,
	{ { T_POINTER } }
	};
    struct vector *difference;
    struct svalue *source,*dest;
    int i;

    vtmp.item[0].u.vec = subtrahend;
    vtmpp = order_alist(&vtmp);
    free_vector(subtrahend);
    subtrahend = vtmpp->item[0].u.vec;
    difference = allocate_array(minuend->size);
    for (source = minuend->item, dest = difference->item, i = minuend->size;
      i--; source++) {
        extern struct svalue const0;

        int assoc PROT((struct svalue *key, struct vector *));
	if (source->type == T_OBJECT && source->u.ob->flags & O_DESTRUCTED)
	    assign_svalue(source, &const0);
	if ( assoc(source, subtrahend) < 0 )
	    assign_svalue_no_free(dest++, source);
    }
    free_vector(vtmpp);
    return shrink_array(difference, dest-difference->item);
}


/* Returns an array of all objects contained in 'ob'
 */
struct vector *all_inventory(ob)
    struct object *ob;
{
    struct vector *d;
    struct object *cur;
    int cnt,res;
    
    cnt=0;
    for (cur=ob->contains; cur; cur = cur->next_inv)
	cnt++;
    
    if (!cnt)
	return allocate_array(0);

    d = allocate_array(cnt);
    cur=ob->contains;
    
    for (res=0;res<cnt;res++) {
	d->item[res].type=T_OBJECT;
	d->item[res].u.ob = cur;
	add_ref(cur,"all_inventory");
	cur=cur->next_inv;
    }
    return d;
}


/* Runs all elements of an array through ob::func
   and replaces each value in arr by the value returned by ob::func
   */
struct vector *map_array (arr, func, ob, extra)
    struct vector *arr;
    char *func;
    struct object *ob;
    struct svalue *extra;
{
    struct vector *r;
    struct svalue *v;
    int cnt;
    
    if (arr->size < 1) 
	return allocate_array(0);

    r = allocate_array(arr->size);
    
    for (cnt = 0; cnt < arr->size; cnt++)
    {
	if (ob->flags & O_DESTRUCTED)
	    error("object used by map_array destructed"); /* amylaar */
	push_svalue(&arr->item[cnt]);

	if (extra)
	{
	    push_svalue (extra);
	    v = apply(func, ob, 2);
	}
	else 
	{
	    v = apply(func,ob,1);
	}
	if (v)
	    assign_svalue_no_free (&r->item[cnt], v);
    }
    return r;
}

static INLINE int sort_array_cmp(func, ob, p1, p2)
    char *func;
    struct object *ob;
    struct svalue *p1,*p2;
{
    struct svalue *d;

    if (ob->flags & O_DESTRUCTED)
        error("object used by sort_array destructed");
    push_svalue(p1);
    push_svalue(p2);
    d = apply(func, ob, 2);
    if (!d) return 0;
    if (d->type != T_NUMBER) {
	/* Amylaar: we expect the object to return a number, and there is
	 *	    no need to use free_svalue on numbers. If, however,
	 *	    some stupid LPC-programmer returns a non-number, memory
	 *	    might be lost unless we free the svalue
	 */
	free_svalue(d);
	return 1;
    }
    return d->u.number > 0;
}

struct vector *sort_array(inlist, func, ob)
    struct vector *inlist;
    char *func;
    struct object *ob;
{
    struct vector *outlist;
    struct svalue *root, *inpnt;
    int keynum, j;

    keynum = inlist->size;
    root = inlist->item;
    /* transform inlist->item[j] into a heap, starting at the top */
    for(j=0,inpnt=root; j<keynum; j++,inpnt++) {
        extern struct svalue const0;
	int curix, parix;
	struct svalue insval;

	/* propagate the new element up in the heap as much as necessary */
	if (inpnt->type == T_OBJECT && inpnt->u.ob->flags & O_DESTRUCTED)
	    assign_svalue(inpnt, &const0);
	insval = *inpnt;
	for(curix = j; curix; curix=parix) {
	    parix = (curix-1)>>1;
	    if ( sort_array_cmp(func, ob, &root[parix], &root[curix] ) ) {
		root[curix] = root[parix];
		root[parix] = insval;
	    }
	}
    }
    outlist = allocate_array(keynum);
    for(j=0; j<keynum; j++) {
	int curix;

	outlist->item[j] = *root;
	for (curix=0; ; ) {
	    int child, child2;

	    child = curix+curix+1;
	    child2 = child+1;
	    if (child2<keynum && root[child2].type != T_INVALID
	      && (root[child].type == T_INVALID ||
		sort_array_cmp(func, ob, &root[child], &root[child2] )
		) )
	    {
		child = child2;
	    }
	    if (child<keynum && root[child].type != T_INVALID) {
		root[curix] = root[child];
		curix = child;
	    } else break;
	}
	root[curix].type = T_INVALID;
    }
    free_vector(inlist);
    return outlist;
}

/* Turns a structured array of elements into a flat array of elements.
   */
static int num_elems(arr)
    struct vector *arr;
{
    int cnt,il;

    cnt = arr->size;

    for (il=0;il<arr->size;il++) 
	if (arr->item[il].type == T_POINTER) 
	    cnt += num_elems(arr->item[il].u.vec) - 1;
    return cnt;
}

struct vector *flatten_array(arr)
    struct vector *arr;
{
    int max, cnt, il, il2;
    struct vector *res, *dres;
    
    if (arr->size < 1) 
	return allocate_array(0);

    max = num_elems(arr);

    if (arr->size == max)
	return arr;

    if (max == 0) 	    /* In the case arr is an array of empty arrays */
	return allocate_array(0);

    res = allocate_array(max); 

    for (cnt = 0 , il = 0; il < arr->size; il++)
	if (arr->item[il].type != T_POINTER) 
	    assign_svalue(&res->item[cnt++],&arr->item[il]);
	else {
	    dres = flatten_array(arr->item[il].u.vec);
	    for (il2=0;il2<dres->size;il2++)
		assign_svalue(&res->item[cnt++],&dres->item[il2]);
	    free_vector(dres);
	}
    
    return res;
}
/*
 * deep_inventory()
 *
 * This function returns the recursive inventory of an object. The returned 
 * array of objects is flat, ie there is no structure reflecting the 
 * internal containment relations.
 *
 */
struct vector *deep_inventory(ob, take_top)
    struct object	*ob;
    int			take_top;
{
    struct vector	*dinv, *ainv, *sinv, *tinv;
    int			il;

    ainv = all_inventory(ob);
    if (take_top)
    {
	sinv = allocate_array(1);
	sinv->item[0].type = T_OBJECT;
	add_ref(ob,"deep_inventory");
	sinv->item[0].u.ob = ob;
	dinv = add_array(sinv, ainv);
	free_vector(sinv);
	free_vector(ainv);
	ainv = dinv;
    }
    sinv = ainv;
    for (il = take_top ? 1 : 0 ; il < ainv->size ; il++)
    {
	if (ainv->item[il].u.ob->contains)
	{
	    dinv = deep_inventory(ainv->item[il].u.ob,0);
	    tinv = add_array(sinv, dinv);
	    if (sinv != ainv)
		free_vector(sinv);
	    sinv = tinv;
	    free_vector(dinv);
	}
    }
    if (ainv != sinv)
	free_vector(ainv);
    return sinv;
}


/* sum_array, processes each element in the array together with the
              results from the previous element. Starting value is 0.
	      Note! This routine allocates a struct svalue which it returns.
	      This value should be pushed to the stack and then freed.
   */
struct svalue *sum_array (arr, func, ob, extra)
    struct vector *arr;
    char *func;
    struct object *ob;
    struct svalue *extra;
{
    struct svalue *ret, v;

    int cnt;

    v.type = T_NUMBER;
    v.u.number = 0;

    for (cnt=0;cnt<arr->size;cnt++) {
	push_svalue(&arr->item[cnt]);
	push_svalue(&v);
	if (extra) {
	    push_svalue (extra);
	    ret = apply(func, ob, 3);
	} else {
	    ret = apply(func,ob,2);
	}
	if (ret)
	    v = *ret;
    }

    ret = (struct svalue *) xalloc(sizeof(struct svalue));
    *ret = v;

    return ret;
}


static INLINE int alist_cmp(p1, p2)
    struct svalue *p1,*p2;
{
    register int d;

    if (d = p1->u.number - p2->u.number) return d;
    if (d = p1->type - p2->type) return d;
    return 0;
}

struct vector *order_alist(inlist)
    struct vector *inlist;
{
    struct vector *outlist;
    struct svalue *inlists, *outlists, *root, *inpnt, *insval;
    int listnum, keynum, i, j;

    listnum = inlist->size;
    inlists = inlist->item;
    keynum = inlists[0].u.vec->size;
    root = inlists[0].u.vec->item;
    /* transform inlists[i].u.vec->item[j] into a heap, starting at the top */
    insval = (struct svalue*)xalloc(sizeof(struct svalue[1])*listnum);
    for(j=0,inpnt=root; j<keynum; j++,inpnt++) {
	int curix, parix;

	/* make sure that strings can be compared by their pointer */
	if (inpnt->type == T_STRING && inpnt->string_type != STRING_SHARED) {
	    char *str = make_shared_string(inpnt->u.string);
	    free_svalue(inpnt);
	    inpnt->type = T_STRING;
	    inpnt->string_type = STRING_SHARED;
	    inpnt->u.string = str;
	}
	/* propagate the new element up in the heap as much as necessary */
	for (i=0; i<listnum; i++) {
	    insval[i] = inlists[i].u.vec->item[j];
	    /* but first ensure that it contains no destructed object */
	    if (insval[i].type == T_OBJECT
	      && insval[i].u.ob->flags & O_DESTRUCTED) {
                extern struct svalue const0;

		free_object(insval[i].u.ob, "order_alist");
	        inlists[i].u.vec->item[j] = insval[i] = const0;
	    }
	}
	for(curix = j; curix; curix=parix) {
	    parix = (curix-1)>>1;
	    if ( alist_cmp(&root[parix], &root[curix]) > 0 ) {
		for (i=0; i<listnum; i++) {
		    inlists[i].u.vec->item[curix] =
		      inlists[i].u.vec->item[parix];
		    inlists[i].u.vec->item[parix] = insval[i];
		}
	    }
	}
    }
    free((char*)insval);
    outlist = allocate_array(listnum);
    outlists = outlist->item;
    for (i=0; i<listnum; i++) {
	outlists[i].type  = T_POINTER;
	outlists[i].u.vec = allocate_array(keynum);
    }
    for(j=0; j<keynum; j++) {
	int curix;

	for (i=0;  i<listnum; i++) {
	    outlists[i].u.vec->item[j] = inlists[i].u.vec->item[0];
	}
	for (curix=0; ; ) {
	    int child, child2;

	    child = curix+curix+1;
	    child2 = child+1;
	    if (child2<keynum && root[child2].type != T_INVALID
	      && (root[child].type == T_INVALID ||
		alist_cmp(&root[child], &root[child2]) > 0))
	    {
		child = child2;
	    }
	    if (child<keynum && root[child].type != T_INVALID) {
		for (i=0; i<listnum; i++) {
		    inlists[i].u.vec->item[curix] =
		      inlists[i].u.vec->item[child];
		}
		curix = child;
	    } else break;
	}
	for (i=0; i<listnum; i++) {
	    inlists[i].u.vec->item[curix].type = T_INVALID;
	}
    }
    return outlist;
}

static int search_alist(key, keylist)
    struct svalue *key;
    struct vector *keylist;
{
    int i, o, d;

    if (!keylist->size) return 0;
    i = (keylist->size) >> 1;
    o = (i+2) >> 1;
    for (;;) {
	d = alist_cmp(key, &keylist->item[i]);
	if (d<0) {
	    i -= o;
	    if (i<0) {
		i = 0;
	    }
	} else if (d>0) {
	    i += o;
	    if (i >= keylist->size) {
	        i = keylist->size-1;
	    }
	} else {
	    return i;
	}
	if (o<=1) {
	    if (alist_cmp(key, &keylist->item[i]) > 0) return i+1;
	    return i;
	}
	o = (o+1) >> 1;
    }
}

#define	STRING_REFS(str)	(*(short *)((char *) (str) - sizeof(short)))

struct svalue *insert_alist(key, key_data, list)
    struct svalue *key, *key_data;
    struct vector *list;
{
    static struct svalue stmp;
    int i,j,ix;
    int keynum;

    if (key->type == T_STRING && key->string_type != STRING_SHARED) {
	static struct svalue stmp2 = { T_STRING, STRING_SHARED };

	stmp2.u.string = make_shared_string(key->u.string);
	STRING_REFS(stmp2.u.string)--;
	/* a reference count will be added again when copied
	   to it's place in the alist */
	key = &stmp2;
    }
    keynum = list->item[0].u.vec->size;
    ix = search_alist(key,list->item[0].u.vec);
    if (key_data == 0) {
	 stmp.type = T_NUMBER;
	 stmp.u.number = ix;
         return &stmp;
    }
    stmp.type = T_POINTER;
    stmp.u.vec = allocate_array(list->size);
    for (i=0; i < list->size; i++) {
	struct vector *vtmp;

        if (ix == keynum || alist_cmp(key, &list->item[0].u.vec->item[ix]) ) {
            struct svalue *pstmp = list->item[i].u.vec->item;
    
	    vtmp = allocate_array(keynum+1);
            for (j=0; j < ix; j++) {
               assign_svalue_no_free(&vtmp->item[j], pstmp++);
            }
            assign_svalue_no_free(&vtmp->item[ix], i ? &key_data[i] : key);
            for (j=ix+1; j <= keynum; j++) {
               assign_svalue_no_free(&vtmp->item[j], pstmp++);
            }
	} else {
	    vtmp = slice_array(list->item[i].u.vec, 0, keynum-1);
	    if (i)
	        assign_svalue(&vtmp->item[ix], &key_data[i]);
	}
	stmp.u.vec->item[i].type=T_POINTER;
	stmp.u.vec->item[i].u.vec=vtmp;
    }
    return &stmp;
}


int assoc(key, list)
    struct svalue *key;
    struct vector *list;
{
    int i;
    extern char* findstring PROT((char*));

    if (key->type == T_STRING && key->string_type != STRING_SHARED) {
        static struct svalue stmp = { T_STRING, STRING_SHARED };

        stmp.u.string = findstring(key->u.string);
        if (!stmp.u.string) return -1;
        key = &stmp;
    }
    i = search_alist(key, list);
    if (i == list->size || alist_cmp(key, &list->item[i]))
        i = -1;
    return i;
}

struct vector *intersect_alist(a1, a2)
    struct vector *a1,*a2;
{
    struct vector *a3;
    int d, l, i1, i2, a1s, a2s;

    a1s = a1->size;
    a2s = a2->size;
    a3 = allocate_array( a1s < a2s ? a1s : a2s);
    for (i1=i2=l=0; i1 < a1s && i2 < a2s; ) {
	d = alist_cmp(&a1->item[i1], &a2->item[i2]);
	if (d<0)
	    i1++;
	else if (d>0)
	    i2++;
	else {
	    assign_svalue_no_free(&a3->item[l++], &a2->item[i1++,i2++]);
	}
    }
    return shrink_array(a3, l);
}

struct vector *symmetric_difference_alist(a1, a2)
    struct vector *a1,*a2;
{
    struct vector *a3;
    int d, l, i1, i2, a1s, a2s;

    a1s = a1->size;
    a2s = a2->size;
    a3 = allocate_array( a1s + a2s );
    for (i1=i2=l=0; i1 < a1s && i2 < a2s; ) {
	d = alist_cmp(&a1->item[i1], &a2->item[i2]);
	if (d<0)
	    assign_svalue_no_free(&a3->item[l++], &a1->item[i1++]);
	else if (d>0)
	    assign_svalue_no_free(&a3->item[l++], &a2->item[i2++]);
	else {
	    i1++;
	    i2++;
	}
    }
    while (i1 < a1s )
	    assign_svalue_no_free(&a3->item[l++], &a1->item[i1++]);
    while (i2 < a2s )
	    assign_svalue_no_free(&a3->item[l++], &a2->item[i2++]);
    return shrink_array(a3, l);
}

struct vector *intersect_array(a1, a2)
    struct vector *a1,*a2;
{
    struct vector *order_alist PROT((struct vector *));
    struct vector *vtmpp1,*vtmpp2,*vtmpp3;
    static struct vector vtmp = { 1, 1,
#ifdef DEBUG
	1,
#endif
	0,
	{ { T_POINTER } }
	};

    if (a1->ref > 1) {
	a1->ref--;
	a1 = slice_array(a1, 0, a1->size-1);
    }
    vtmp.item[0].u.vec = a1;
    vtmpp1 = order_alist(&vtmp);
    free_vector(vtmp.item[0].u.vec);
    if (a2->ref > 1) {
	a2->ref--;
	a2 = slice_array(a2, 0, a2->size-1);
    }
    vtmp.item[0].u.vec = a2;
    vtmpp2 = order_alist(&vtmp);
    free_vector(vtmp.item[0].u.vec);
    vtmpp3 = intersect_alist(vtmpp1->item[0].u.vec, vtmpp2->item[0].u.vec);
    free_vector(vtmpp1);
    free_vector(vtmpp2);
    return vtmpp3;
}

struct vector *match_regexp(v, pattern)
    struct vector *v;
    char *pattern;
{
    struct regexp *reg;
    char *res;
    int i, num_match;
    struct vector *ret;
    extern int eval_cost;

    if (v->size == 0)
	return allocate_array(0);
    reg = regcomp(pattern, 0);
    if (reg == 0)
	return 0;
    res = (char *)alloca(v->size);
    for (num_match=i=0; i < v->size; i++) {
	res[i] = 0;
	if (v->item[i].type != T_STRING)
	    continue;
	eval_cost++;
	if (regexec(reg, v->item[i].u.string) == 0)
	    continue;
	res[i] = 1;
	num_match++;
    }
    ret = allocate_array(num_match);
    for (num_match=i=0; i < v->size; i++) {
	if (res[i] == 0)
	    continue;
	assign_svalue_no_free(&ret->item[num_match], &v->item[i]);
	num_match++;
    }
    free((char *)reg);
    return ret;
}

/*
 * Returns a list of all inherited files.
 *
 * Must be fixed so that any number of files can be returned, now max 256
 * (Sounds like a contradiction to me /Lars).
 */
struct vector *inherit_list(ob)
    struct object *ob;
{
    struct vector *ret;
    struct program *pr, *plist[256];
    int il, il2, next, cur;

    plist[0] = ob->prog; next = 1; cur = 0;
    
    for (; cur < next && next < 256; cur++)
    {
	pr = plist[cur];
	for (il2 = 0; il2 < pr->num_inherited; il2++)
	    plist[next++] = pr->inherit[il2].prog;
    }
	    
    ret = allocate_array(next);

    for (il = 0; il < next; il++)
    {
	pr = plist[il];
	ret->item[il].type = T_STRING;
	ret->item[il].string_type = STRING_SHARED;
	ret->item[il].u.string = 
		make_shared_string(pr->name);
    }
    return ret;
}
