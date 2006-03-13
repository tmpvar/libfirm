/**
 * This file implements the creation of the achitecture specific firm opcodes
 * and the coresponding node constructors for the $arch assembler irg.
 * @author Christian Wuerdig
 * $Id$
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _WIN32
#include <malloc.h>
#else
#include <alloca.h>
#endif

#include <stdlib.h>

#include "irprog_t.h"
#include "irgraph_t.h"
#include "irnode_t.h"
#include "irmode_t.h"
#include "ircons_t.h"
#include "iropt_t.h"
#include "irop.h"
#include "firm_common_t.h"
#include "irvrfy_t.h"
#include "irprintf.h"

#include "../bearch.h"

#include "ia32_nodes_attr.h"
#include "ia32_new_nodes.h"
#include "gen_ia32_regalloc_if.h"

#ifdef obstack_chunk_alloc
# undef obstack_chunk_alloc
# define obstack_chunk_alloc xmalloc
#else
# define obstack_chunk_alloc xmalloc
# define obstack_chunk_free free
#endif

extern int obstack_printf(struct obstack *obst, char *fmt, ...);

/***********************************************************************************
 *      _                                   _       _             __
 *     | |                                 (_)     | |           / _|
 *   __| |_   _ _ __ ___  _ __   ___ _ __   _ _ __ | |_ ___ _ __| |_ __ _  ___ ___
 *  / _` | | | | '_ ` _ \| '_ \ / _ \ '__| | | '_ \| __/ _ \ '__|  _/ _` |/ __/ _ \
 * | (_| | |_| | | | | | | |_) |  __/ |    | | | | | ||  __/ |  | || (_| | (_|  __/
 *  \__,_|\__,_|_| |_| |_| .__/ \___|_|    |_|_| |_|\__\___|_|  |_| \__,_|\___\___|
 *                       | |
 *                       |_|
 ***********************************************************************************/

/**
 * Returns the name of a SymConst.
 * @param symc  the SymConst
 * @return name of the SymConst
 */
const char *get_sc_name(ir_node *symc) {
	if (get_irn_opcode(symc) != iro_SymConst)
		return "NONE";

	switch (get_SymConst_kind(symc)) {
		case symconst_addr_name:
			return get_id_str(get_SymConst_name(symc));

		case symconst_addr_ent:
			return get_entity_ld_name(get_SymConst_entity(symc));

		default:
			assert(0 && "Unsupported SymConst");
	}

	return NULL;
}

/**
 * Returns a string containing the names of all registers within the limited bitset
 */
static char *get_limited_regs(const arch_register_req_t *req, char *buf, int max) {
	bitset_t *bs   = bitset_alloca(req->cls->n_regs);
	char     *p    = buf;
	int       size = 0;
	int       i, cnt;

	req->limited(NULL, bs);

	for (i = 0; i < req->cls->n_regs; i++) {
		if (bitset_is_set(bs, i)) {
			cnt = snprintf(p, max - size, " %s", req->cls->regs[i].name);
			if (cnt < 0) {
				fprintf(stderr, "dumper problem, exiting\n");
				exit(1);
			}

			p    += cnt;
			size += cnt;

			if (size >= max)
				break;
		}
	}

	return buf;
}

/**
 * Dumps the register requirements for either in or out.
 */
static void dump_reg_req(FILE *F, ir_node *n, const ia32_register_req_t **reqs, int inout) {
	char *dir = inout ? "out" : "in";
	int   max = inout ? get_ia32_n_res(n) : get_irn_arity(n);
	char *buf = alloca(1024);
	int   i;

	memset(buf, 0, 1024);

	if (reqs) {
		for (i = 0; i < max; i++) {
			fprintf(F, "%sreq #%d =", dir, i);

			if (reqs[i]->req.type == arch_register_req_type_none) {
				fprintf(F, " n/a");
			}

			if (reqs[i]->req.type & arch_register_req_type_normal) {
				fprintf(F, " %s", reqs[i]->req.cls->name);
			}

			if (reqs[i]->req.type & arch_register_req_type_limited) {
				fprintf(F, " %s", get_limited_regs(&reqs[i]->req, buf, 1024));
			}

			if (reqs[i]->req.type & arch_register_req_type_should_be_same) {
				ir_fprintf(F, " same as %+F", get_irn_n(n, reqs[i]->same_pos));
			}

			if (reqs[i]->req.type & arch_register_req_type_should_be_different) {
				ir_fprintf(F, " different from %+F", get_irn_n(n, reqs[i]->different_pos));
			}

			fprintf(F, "\n");
		}

		fprintf(F, "\n");
	}
	else {
		fprintf(F, "%sreq = N/A\n", dir);
	}
}

/**
 * Dumper interface for dumping ia32 nodes in vcg.
 * @param n        the node to dump
 * @param F        the output file
 * @param reason   indicates which kind of information should be dumped
 * @return 0 on success or != 0 on failure
 */
static int dump_node_ia32(ir_node *n, FILE *F, dump_reason_t reason) {
	ir_mode     *mode = NULL;
	int          bad  = 0;
	int          i, n_res, am_flav, flags;
	const ia32_register_req_t **reqs;
	const arch_register_t     **slots;

	switch (reason) {
		case dump_node_opcode_txt:
			fprintf(F, "%s", get_irn_opname(n));
			break;

		case dump_node_mode_txt:
			mode = get_irn_mode(n);

			if (is_ia32_Ld(n) || is_ia32_St(n)) {
				mode = get_ia32_ls_mode(n);
			}

			fprintf(F, "[%s]", mode ? get_mode_name(mode) : "?NOMODE?");
			break;

		case dump_node_nodeattr_txt:
			if (get_ia32_cnst(n)) {
				char *pref = "";

				if (get_ia32_sc(n)) {
					pref = "SymC ";
				}

				fprintf(F, "[%s%s]", pref, get_ia32_cnst(n));
			}

			if (! is_ia32_Lea(n)) {
				if (is_ia32_AddrModeS(n)) {
					fprintf(F, "[AM S] ");
				}
				else if (is_ia32_AddrModeD(n)) {
					fprintf(F, "[AM D] ");
				}
			}

			break;

		case dump_node_info_txt:
			n_res = get_ia32_n_res(n);
			fprintf(F, "=== IA32 attr begin ===\n");

			/* dump IN requirements */
			if (get_irn_arity(n) > 0) {
				reqs = get_ia32_in_req_all(n);
				dump_reg_req(F, n, reqs, 0);
			}

			/* dump OUT requirements */
			if (n_res > 0) {
				reqs = get_ia32_out_req_all(n);
				dump_reg_req(F, n, reqs, 1);
			}

			/* dump assigned registers */
			slots = get_ia32_slots(n);
			if (slots && n_res > 0) {
				for (i = 0; i < n_res; i++) {
					fprintf(F, "reg #%d = %s\n", i, slots[i] ? slots[i]->name : "n/a");
				}
				fprintf(F, "\n");
			}

			/* dump op type */
			fprintf(F, "op = ");
			switch (get_ia32_op_type(n)) {
				case ia32_Normal:
					fprintf(F, "Normal");
					break;
				case ia32_Const:
					fprintf(F, "Const");
					break;
				case ia32_SymConst:
					fprintf(F, "SymConst");
					break;
				case ia32_AddrModeD:
					fprintf(F, "AM Dest (Load+Store)");
					break;
				case ia32_AddrModeS:
					fprintf(F, "AM Source (Load)");
					break;
				default:
					fprintf(F, "unknown (%d)", get_ia32_op_type(n));
					break;
			}
			fprintf(F, "\n");


			/* dump supported am */
			fprintf(F, "AM support = ");
			switch (get_ia32_am_support(n)) {
				case ia32_am_None:
					fprintf(F, "none");
					break;
				case ia32_am_Source:
					fprintf(F, "source only (Load)");
					break;
				case ia32_am_Dest:
					fprintf(F, "dest only (Load+Store)");
					break;
				case ia32_am_Full:
					fprintf(F, "full");
					break;
				default:
					fprintf(F, "unknown (%d)", get_ia32_am_support(n));
					break;
			}
			fprintf(F, "\n");

			/* dump am flavour */
			fprintf(F, "AM flavour =");
			am_flav = get_ia32_am_flavour(n);
			if (am_flav == ia32_am_N) {
				fprintf(F, " none");
			}
			else {
				if (am_flav & ia32_O) {
					fprintf(F, " O");
				}
				if (am_flav & ia32_B) {
					fprintf(F, " B");
				}
				if (am_flav & ia32_I) {
					fprintf(F, " I");
				}
				if (am_flav & ia32_S) {
					fprintf(F, " S");
				}
			}
			fprintf(F, " (%d)\n", am_flav);

			/* dump AM offset */
			fprintf(F, "AM offset = ");
			if (get_ia32_am_offs(n)) {
				fprintf(F, "%s", get_ia32_am_offs(n));
			}
			else {
				fprintf(F, "n/a");
			}
			fprintf(F, "\n");

			/* dump AM scale */
			fprintf(F, "AM scale = %d\n", get_ia32_am_scale(n));

			/* dump pn code */
			fprintf(F, "pn_code = %ld\n", get_ia32_pncode(n));

			/* dump n_res */
			fprintf(F, "n_res = %d\n", get_ia32_n_res(n));

			/* dump use_frame */
			fprintf(F, "use_frame = %d\n", is_ia32_use_frame(n));

			/* commutative */
			fprintf(F, "commutative = %d\n", is_ia32_commutative(n));

			/* dump flags */
			fprintf(F, "flags =");
			flags = get_ia32_flags(n);
			if (flags == arch_irn_flags_none) {
				fprintf(F, " none");
			}
			else {
				if (flags & arch_irn_flags_dont_spill) {
					fprintf(F, " unspillable");
				}
				if (flags & arch_irn_flags_rematerializable) {
					fprintf(F, " remat");
				}
				if (flags & arch_irn_flags_ignore) {
					fprintf(F, " ignore");
				}
			}
			fprintf(F, " (%d)\n", flags);

			/* dump frame entity */
			fprintf(F, "frame entity = ");
			if (get_ia32_frame_ent(n)) {
				ir_fprintf(F, "%+F", get_ia32_frame_ent(n));
			}
			else {
				fprintf(F, "n/a");
			}
			fprintf(F, "\n");

#ifndef NDEBUG
			/* dump original ir node name */
			fprintf(F, "orig node = ");
			if (get_ia32_orig_node(n)) {
				fprintf(F, "%s", get_ia32_orig_node(n));
			}
			else {
				fprintf(F, "n/a");
			}
			fprintf(F, "\n");
#endif /* NDEBUG */

			fprintf(F, "=== IA32 attr end ===\n");
			/* end of: case dump_node_info_txt */
			break;
	}

	return bad;
}



/***************************************************************************************************
 *        _   _                   _       __        _                    _   _               _
 *       | | | |                 | |     / /       | |                  | | | |             | |
 *   __ _| |_| |_ _ __   ___  ___| |_   / /_ _  ___| |_   _ __ ___   ___| |_| |__   ___   __| |___
 *  / _` | __| __| '__| / __|/ _ \ __| / / _` |/ _ \ __| | '_ ` _ \ / _ \ __| '_ \ / _ \ / _` / __|
 * | (_| | |_| |_| |    \__ \  __/ |_ / / (_| |  __/ |_  | | | | | |  __/ |_| | | | (_) | (_| \__ \
 *  \__,_|\__|\__|_|    |___/\___|\__/_/ \__, |\___|\__| |_| |_| |_|\___|\__|_| |_|\___/ \__,_|___/
 *                                        __/ |
 *                                       |___/
 ***************************************************************************************************/

 static char *copy_str(char *dst, const char *src) {
	 dst = xcalloc(1, strlen(src) + 1);
	 strncpy(dst, src, strlen(src) + 1);
	 return dst;
 }

 static char *set_cnst_from_tv(char *cnst, tarval *tv) {
	 if (cnst) {
		 free(cnst);
	 }

	 cnst = xcalloc(1, 64);
	 assert(tarval_snprintf(cnst, 63, tv));
	 return cnst;
 }

/**
 * Wraps get_irn_generic_attr() as it takes no const ir_node, so we need to do a cast.
 * Firm was made by people hating const :-(
 */
ia32_attr_t *get_ia32_attr(const ir_node *node) {
	assert(is_ia32_irn(node) && "need ia32 node to get ia32 attributes");
	return (ia32_attr_t *)get_irn_generic_attr((ir_node *)node);
}

/**
 * Gets the type of an ia32 node.
 */
ia32_op_type_t get_ia32_op_type(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->data.tp;
}

/**
 * Sets the type of an ia32 node.
 */
void set_ia32_op_type(ir_node *node, ia32_op_type_t tp) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->data.tp     = tp;
}

/**
 * Gets the supported addrmode of an ia32 node
 */
ia32_am_type_t get_ia32_am_support(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->data.am_support;
}

/**
 * Sets the supported addrmode of an ia32 node
 */
void set_ia32_am_support(ir_node *node, ia32_am_type_t am_tp) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->data.am_support  = am_tp;
}

/**
 * Gets the addrmode flavour of an ia32 node
 */
ia32_am_flavour_t get_ia32_am_flavour(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->data.am_flavour;
}

/**
 * Sets the addrmode flavour of an ia32 node
 */
void set_ia32_am_flavour(ir_node *node, ia32_am_flavour_t am_flavour) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->data.am_flavour  = am_flavour;
}

/**
 * Joins all offsets to one string with adds.
 */
char *get_ia32_am_offs(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	char        *res  = NULL;
	int          size;

	if (! attr->am_offs) {
		return NULL;
	}

	size = obstack_object_size(attr->am_offs);
    if (size > 0) {
		res    = xcalloc(1, size + 2);
		res[0] = attr->data.offs_sign ? '-' : '+';
		memcpy(&res[1], obstack_base(attr->am_offs), size);
    }

	res[size + 1] = '\0';
	return res;
}

/**
 * Add an offset for addrmode.
 */
static void extend_ia32_am_offs(ir_node *node, char *offset, char op) {
	ia32_attr_t *attr = get_ia32_attr(node);

	if (! offset)
		return;

	/* offset could already have an explicit sign */
	/* -> supersede op if necessary               */
	if (offset[0] == '-' || offset[0] == '+') {
		if (offset[0] == '-') {
			op = (op == '-') ? '+' : '-';
		}

		/* skip explicit sign */
		offset++;
	}

	if (! attr->am_offs) {
		/* obstack is not initialized */
		attr->am_offs = xcalloc(1, sizeof(*(attr->am_offs)));
		obstack_init(attr->am_offs);

		attr->data.offs_sign = (op == '-') ? 1 : 0;
	}
	else {
		/* If obstack is initialized, connect the new offset with op */
		obstack_printf(attr->am_offs, "%c", op);
	}

	obstack_printf(attr->am_offs, "%s", offset);
}

/**
 * Add an offset for addrmode.
 */
void add_ia32_am_offs(ir_node *node, char *offset) {
	extend_ia32_am_offs(node, offset, '+');
}

/**
 * Sub an offset for addrmode.
 */
void sub_ia32_am_offs(ir_node *node, char *offset) {
	extend_ia32_am_offs(node, offset, '-');
}

/**
 * Gets the addr mode const.
 */
int get_ia32_am_scale(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->data.am_scale;
}

/**
 * Sets the index register scale for addrmode.
 */
void set_ia32_am_scale(ir_node *node, int scale) {
	ia32_attr_t *attr   = get_ia32_attr(node);
	attr->data.am_scale = scale;
}

/**
 * Return the tarval of an immediate operation or NULL in case of SymConst
 */
tarval *get_ia32_Immop_tarval(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
    return attr->tv;
}

/**
 * Sets the attributes of an immediate operation to the specified tarval
 */
void set_ia32_Immop_tarval(ir_node *node, tarval *tv) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->tv          = tv;
	attr->cnst        = set_cnst_from_tv(attr->cnst, attr->tv);
}

/**
 * Return the sc attribute.
 */
char *get_ia32_sc(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->sc;
}

/**
 * Sets the sc attribute.
 */
void set_ia32_sc(ir_node *node, char *sc) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->sc          = copy_str(attr->sc, sc);

	if (attr->cnst) {
		free(attr->cnst);
	}
	attr->cnst = attr->sc;
}

/**
 * Gets the string representation of the internal const (tv or symconst)
 */
char *get_ia32_cnst(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->cnst;
}

/**
 * Sets the uses_frame flag.
 */
void set_ia32_use_frame(ir_node *node) {
	ia32_attr_t *attr    = get_ia32_attr(node);
	attr->data.use_frame = 1;
}

/**
 * Clears the uses_frame flag.
 */
void clear_ia32_use_frame(ir_node *node) {
	ia32_attr_t *attr    = get_ia32_attr(node);
	attr->data.use_frame = 0;
}

/**
 * Gets the uses_frame flag.
 */
int is_ia32_use_frame(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->data.use_frame;
}

/**
 * Sets node to commutative.
 */
void set_ia32_commutative(ir_node *node) {
	ia32_attr_t *attr         = get_ia32_attr(node);
	attr->data.is_commutative = 1;
}

/**
 * Sets node to non-commutative.
 */
void clear_ia32_commutative(ir_node *node) {
	ia32_attr_t *attr         = get_ia32_attr(node);
	attr->data.is_commutative = 0;
}

/**
 * Checks if node is commutative.
 */
int is_ia32_commutative(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->data.is_commutative;
}

/**
 * Gets the mode of the stored/loaded value (only set for Store/Load)
 */
ir_mode *get_ia32_ls_mode(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->ls_mode;
}

/**
 * Sets the mode of the stored/loaded value (only set for Store/Load)
 */
void set_ia32_ls_mode(ir_node *node, ir_mode *mode) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->ls_mode     = mode;
}

/**
 * Gets the frame entity assigned to this node;
 */
entity *get_ia32_frame_ent(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->frame_ent;
}

/**
 * Sets the frame entity for this node;
 */
void set_ia32_frame_ent(ir_node *node, entity *ent) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->frame_ent   = ent;
}

/**
 * Returns the argument register requirements of an ia32 node.
 */
const ia32_register_req_t **get_ia32_in_req_all(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->in_req;
}

/**
 * Sets the argument register requirements of an ia32 node.
 */
void set_ia32_in_req_all(ir_node *node, const ia32_register_req_t **reqs) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->in_req      = reqs;
}

/**
 * Returns the result register requirements of an ia32 node.
 */
const ia32_register_req_t **get_ia32_out_req_all(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->out_req;
}

/**
 * Sets the result register requirements of an ia32 node.
 */
void set_ia32_out_req_all(ir_node *node, const ia32_register_req_t **reqs) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->out_req     = reqs;
}

/**
 * Returns the argument register requirement at position pos of an ia32 node.
 */
const ia32_register_req_t *get_ia32_in_req(const ir_node *node, int pos) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->in_req[pos];
}

/**
 * Returns the result register requirement at position pos of an ia32 node.
 */
const ia32_register_req_t *get_ia32_out_req(const ir_node *node, int pos) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->out_req[pos];
}

/**
 * Sets the OUT register requirements at position pos.
 */
void set_ia32_req_out(ir_node *node, const ia32_register_req_t *req, int pos) {
	ia32_attr_t *attr  = get_ia32_attr(node);
	attr->out_req[pos] = req;
}

/**
 * Sets the IN register requirements at position pos.
 */
void set_ia32_req_in(ir_node *node, const ia32_register_req_t *req, int pos) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->in_req[pos] = req;
}

/**
 * Returns the register flag of an ia32 node.
 */
arch_irn_flags_t get_ia32_flags(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->data.flags;
}

/**
 * Sets the register flag of an ia32 node.
 */
void set_ia32_flags(ir_node *node, arch_irn_flags_t flags) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->data.flags  = flags;
}

/**
 * Returns the result register slots of an ia32 node.
 */
const arch_register_t **get_ia32_slots(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->slots;
}

/**
 * Sets the number of results.
 */
void set_ia32_n_res(ir_node *node, int n_res) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->data.n_res  = n_res;
}

/**
 * Returns the number of results.
 */
int get_ia32_n_res(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->data.n_res;
}

/**
 * Returns the flavour of an ia32 node,
 */
ia32_op_flavour_t get_ia32_flavour(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->data.op_flav;
}

/**
 * Sets the flavour of an ia32 node to flavour_Div/Mod/DivMod/Mul/Mulh.
 */
void set_ia32_flavour(ir_node *node, ia32_op_flavour_t op_flav) {
	ia32_attr_t *attr  = get_ia32_attr(node);
	attr->data.op_flav = op_flav;
}

/**
 * Returns the projnum code.
 */
long get_ia32_pncode(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->pn_code;
}

/**
 * Sets the projnum code
 */
void set_ia32_pncode(ir_node *node, long code) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->pn_code     = code;
}

#ifndef NDEBUG

/**
 * Returns the name of the original ir node.
 */
const char *get_ia32_orig_node(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return attr->orig_node;
}

/**
 * Sets the name of the original ir node.
 */
void set_ia32_orig_node(ir_node *node, const char *name) {
	ia32_attr_t *attr = get_ia32_attr(node);
	attr->orig_node   = name;
}

#endif /* NDEBUG */

/******************************************************************************************************
 *                      _       _         _   _           __                  _   _
 *                     (_)     | |       | | | |         / _|                | | (_)
 *  ___ _ __   ___  ___ _  __ _| |   __ _| |_| |_ _ __  | |_ _   _ _ __   ___| |_ _  ___  _ __    ___
 * / __| '_ \ / _ \/ __| |/ _` | |  / _` | __| __| '__| |  _| | | | '_ \ / __| __| |/ _ \| '_ \  / __|
 * \__ \ |_) |  __/ (__| | (_| | | | (_| | |_| |_| |    | | | |_| | | | | (__| |_| | (_) | | | | \__ \
 * |___/ .__/ \___|\___|_|\__,_|_|  \__,_|\__|\__|_|    |_|  \__,_|_| |_|\___|\__|_|\___/|_| |_| |___/
 *     | |
 *     |_|
 ******************************************************************************************************/

/**
 * Gets the type of an ia32_Const.
 */
unsigned get_ia32_Const_type(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);

	assert(is_ia32_Cnst(node) && "Need ia32_Const to get type");

	return attr->data.tp;
}

/**
 * Sets the type of an ia32_Const.
 */
void set_ia32_Const_type(ir_node *node, int type) {
	ia32_attr_t *attr = get_ia32_attr(node);

	assert(is_ia32_Cnst(node) && "Need ia32_Const to set type");
	assert((type == ia32_Const || type == ia32_SymConst) && "Unsupported ia32_Const type");

	attr->data.tp = type;
}

/**
 * Copy the attributes from an ia32_Const to an Immop (Add_i, Sub_i, ...) node
 */
void set_ia32_Immop_attr(ir_node *node, ir_node *cnst) {
	ia32_attr_t *na = get_ia32_attr(node);
	ia32_attr_t *ca = get_ia32_attr(cnst);

	assert(is_ia32_Cnst(cnst) && "Need ia32_Const to set Immop attr");

	na->tv = ca->tv;

	if (ca->sc) {
		na->sc   = copy_str(na->sc, ca->sc);
		na->cnst = na->sc;
	}
	else {
		na->cnst = set_cnst_from_tv(na->cnst, na->tv);
		na->sc   = NULL;
	}
}

/**
 * Copy the attributes from a Const to an ia32_Const
 */
void set_ia32_Const_attr(ir_node *ia32_cnst, ir_node *cnst) {
	ia32_attr_t *attr = get_ia32_attr(ia32_cnst);

	assert(is_ia32_Cnst(ia32_cnst) && "Need ia32_Const to set Const attr");

	switch (get_irn_opcode(cnst)) {
		case iro_Const:
			attr->data.tp = ia32_Const;
			attr->tv      = get_Const_tarval(cnst);
			attr->cnst    = set_cnst_from_tv(attr->cnst, attr->tv);
			break;
		case iro_SymConst:
			attr->data.tp = ia32_SymConst;
			attr->tv      = NULL;
			attr->sc      = copy_str(attr->sc, get_sc_name(cnst));
			attr->cnst    = attr->sc;
			break;
		case iro_Unknown:
			assert(0 && "Unknown Const NYI");
			break;
		default:
			assert(0 && "Cannot create ia32_Const for this opcode");
	}
}

/**
 * Sets the AddrMode(S|D) attribute
 */
void set_ia32_AddrMode(ir_node *node, char direction) {
	ia32_attr_t *attr = get_ia32_attr(node);

	switch (direction) {
		case 'D':
			attr->data.tp = ia32_AddrModeD;
			break;
		case 'S':
			attr->data.tp = ia32_AddrModeS;
			break;
		default:
			assert(0 && "wrong AM type");
	}
}

/**
 * Returns whether or not the node is an AddrModeS node.
 */
int is_ia32_AddrModeS(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return (attr->data.tp == ia32_AddrModeS);
}

/**
 * Returns whether or not the node is an AddrModeD node.
 */
int is_ia32_AddrModeD(const ir_node *node) {
	ia32_attr_t *attr = get_ia32_attr(node);
	return (attr->data.tp == ia32_AddrModeD);
}

/**
 * Checks if node is a Load or fLoad.
 */
int is_ia32_Ld(const ir_node *node) {
	return is_ia32_Load(node) || is_ia32_fLoad(node);
}

/**
 * Checks if node is a Store or fStore.
 */
int is_ia32_St(const ir_node *node) {
	return is_ia32_Store(node) || is_ia32_fStore(node);
}

/**
 * Checks if node is a Const or fConst.
 */
int is_ia32_Cnst(const ir_node *node) {
	return is_ia32_Const(node) || is_ia32_fConst(node);
}

/**
 * Returns the name of the OUT register at position pos.
 */
const char *get_ia32_out_reg_name(const ir_node *node, int pos) {
	ia32_attr_t *attr = get_ia32_attr(node);

	assert(is_ia32_irn(node) && "Not an ia32 node.");
	assert(pos < attr->data.n_res && "Invalid OUT position.");
	assert(attr->slots[pos]  && "No register assigned");

	return arch_register_get_name(attr->slots[pos]);
}

/**
 * Returns the index of the OUT register at position pos within its register class.
 */
int get_ia32_out_regnr(const ir_node *node, int pos) {
	ia32_attr_t *attr = get_ia32_attr(node);

	assert(is_ia32_irn(node) && "Not an ia32 node.");
	assert(pos < attr->data.n_res && "Invalid OUT position.");
	assert(attr->slots[pos]  && "No register assigned");

	return arch_register_get_index(attr->slots[pos]);
}

/**
 * Returns the OUT register at position pos.
 */
const arch_register_t *get_ia32_out_reg(const ir_node *node, int pos) {
	ia32_attr_t *attr = get_ia32_attr(node);

	assert(is_ia32_irn(node) && "Not an ia32 node.");
	assert(pos < attr->data.n_res && "Invalid OUT position.");
	assert(attr->slots[pos]  && "No register assigned");

	return attr->slots[pos];
}

/**
 * Allocates num register slots for node.
 */
void alloc_ia32_reg_slots(ir_node *node, int num) {
	ia32_attr_t *attr = get_ia32_attr(node);

	if (num) {
		attr->slots = xcalloc(num, sizeof(attr->slots[0]));
	}
	else {
		attr->slots = NULL;
	}

	attr->data.n_res = num;
}

/**
 * Initializes the nodes attributes.
 */
void init_ia32_attributes(ir_node *node, arch_irn_flags_t flags, const ia32_register_req_t **in_reqs,
						  const ia32_register_req_t **out_reqs, int n_res)
{
	set_ia32_flags(node, flags);
	set_ia32_in_req_all(node, in_reqs);
	set_ia32_out_req_all(node, out_reqs);
	alloc_ia32_reg_slots(node, n_res);
}

/***************************************************************************************
 *                  _                            _                   _
 *                 | |                          | |                 | |
 *  _ __   ___   __| | ___    ___ ___  _ __  ___| |_ _ __ _   _  ___| |_ ___  _ __ ___
 * | '_ \ / _ \ / _` |/ _ \  / __/ _ \| '_ \/ __| __| '__| | | |/ __| __/ _ \| '__/ __|
 * | | | | (_) | (_| |  __/ | (_| (_) | | | \__ \ |_| |  | |_| | (__| || (_) | |  \__ \
 * |_| |_|\___/ \__,_|\___|  \___\___/|_| |_|___/\__|_|   \__,_|\___|\__\___/|_|  |___/
 *
 ***************************************************************************************/

/* default compare operation to compare immediate ops */
int ia32_compare_immop_attr(ia32_attr_t *a, ia32_attr_t *b) {
	if (a->data.tp == b->data.tp) {
		if (! a->cnst || ! b->cnst)
			return 1;

		return strcmp(a->cnst, b->cnst);
	}

	return 1;
}

/* Include the generated constructor functions */
#include "gen_ia32_new_nodes.c.inl"
