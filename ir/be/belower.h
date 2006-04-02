/**
 * Chordal register allocation.
 * @author Christian Wuerdig
 * @date 2005/12/14
 * @cvsid $Id$
 */

#ifndef _BELOWER_H_
#define _BELOWER_H_

#include "bechordal.h"
#include "be_t.h"

void assure_constraints(be_irg_t *birg);
void lower_nodes_after_ra(be_chordal_env_t *chord_env, int do_copy, int do_stat);

#endif /* _BELOWER_H_ */
