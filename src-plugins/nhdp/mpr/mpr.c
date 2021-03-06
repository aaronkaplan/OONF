
/*
 * The olsr.org Optimized Link-State Routing daemon version 2 (olsrd2)
 * Copyright (c) 2004-2015, the olsr.org team - see HISTORY file
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 */

/**
 * @file
 */

#include <errno.h>
#include <stdio.h>

#include "common/common_types.h"
#include "common/autobuf.h"
#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/container_of.h"
#include "config/cfg_schema.h"
#include "core/oonf_logging.h"
#include "core/oonf_subsystem.h"
#include "subsystems/oonf_class.h"
#include "subsystems/oonf_rfc5444.h"
#include "subsystems/oonf_timer.h"

#include "nhdp/nhdp.h"
#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_domain.h"
#include "nhdp/nhdp_interfaces.h"

#include "mpr/mpr.h"

#include "neighbor-graph-flooding.h"
#include "neighbor-graph-routing.h"
#include "selection-rfc7181.h"

/* FIXME remove unneeded includes */

/* definitions */
#define LOG_MPR _nhdp_mpr_subsystem.logging

/* prototypes */
static int _init(void);
static void _cleanup(void);
static void _cb_update_mpr(void);

static const char *_dependencies[] = {
  OONF_CLASS_SUBSYSTEM,
  OONF_TIMER_SUBSYSTEM,
  OONF_NHDP_SUBSYSTEM,
};
static struct oonf_subsystem _nhdp_mpr_subsystem = {
  .name = OONF_MPR_SUBSYSTEM,
  .dependencies = _dependencies,
  .dependencies_count = ARRAYSIZE(_dependencies),
  .descr = "RFC7181 Appendix B MPR Plugin",
  .author = "Jonathan Kirchhoff",

  .init = _init,
  .cleanup = _cleanup,
};
DECLARE_OONF_PLUGIN(_nhdp_mpr_subsystem);

static struct nhdp_domain_mpr _mpr_handler = {
  .name = OONF_MPR_SUBSYSTEM,
  .update_mpr = _cb_update_mpr,
};

/**
 * Initialize plugin
 * @return -1 if an error happened, 0 otherwise
 */
static int
_init(void) {
  if (nhdp_domain_mpr_add(&_mpr_handler)) {
    return -1;
  }
  return 0;
}

/**
 * Cleanup plugin
 */
static void
_cleanup(void) {
}

/**
 * Updates the current routing MPR selection in the NHDP database
 * @param current_mpr_data
 */
static void
_update_nhdp_routing(struct neighbor_graph *graph) {
  struct nhdp_link *lnk;
  struct n1_node *current_mpr_node;
#ifdef OONF_LOG_DEBUG_INFO
  struct netaddr_str buf1;
#endif

  OONF_DEBUG(LOG_MPR, "Updating ROUTING MPRs");
  
  list_for_each_element(nhdp_db_get_link_list(), lnk, _global_node) {
    lnk->neigh->_domaindata[0].neigh_is_mpr = false;
    current_mpr_node = avl_find_element(&graph->set_mpr,
        &lnk->neigh->originator,
        current_mpr_node, _avl_node);
    if (current_mpr_node != NULL) {
      OONF_DEBUG(LOG_MPR, "Processing MPR node %s",
          netaddr_to_string(&buf1, &current_mpr_node->addr));
      lnk->neigh->_domaindata[0].neigh_is_mpr = true;
    }
  }
}

/**
 * Updates the current flooding MPR selection in the NHDP database
 * @param current_mpr_data
 */
static void
_update_nhdp_flooding(struct neighbor_graph *graph) {
  struct nhdp_link *current_link;
  struct n1_node *current_mpr_node;
#ifdef OONF_LOG_DEBUG_INFO
  struct netaddr_str buf1;
#endif

  OONF_DEBUG(LOG_MPR, "Updating FLOODING MPRs");

  list_for_each_element(nhdp_db_get_link_list(), current_link, _global_node) {
    current_mpr_node = avl_find_element(&graph->set_mpr,
        &current_link->neigh->originator,
        current_mpr_node, _avl_node);
    if (current_mpr_node != NULL) {
      OONF_DEBUG(LOG_MPR, "Processing MPR node %s",
          netaddr_to_string(&buf1, &current_mpr_node->addr));
      current_link->neigh->neigh_is_flooding_mpr = true;
    }
  }
}

/**
 * Updates the current flooding MPR selection in the NHDP database
 * @param current_mpr_data
 */
static void
_clear_nhdp_flooding(void) {
  struct nhdp_link *current_link;

  OONF_DEBUG(LOG_MPR, "Updating FLOODING MPRs");

  list_for_each_element(nhdp_db_get_link_list(), current_link, _global_node) {
    current_link->neigh->neigh_is_flooding_mpr = false;
  }
}

static void
_update_flooding_mpr(void) {
  struct mpr_flooding_data flooding_data;

  memset(&flooding_data, 0, sizeof(flooding_data));
  
  if (nhdp_domain_get_flooding()->mpr != &_mpr_handler) {
    /* we are not the flooding mpr */
    return;
  }

  /* FIXME Currently, the flooding set is calculated incrementally (i.e. 
   in a coordinated way as suggested by RFC 7181; however, this should
   be configurable (and other selection algorithms might not be compatible
   with this approach).
   */
  /* FIXME How to support the coordination flooding and routing MPRs 
   * selection? */
  /* calculate flooding MPRs */
  _clear_nhdp_flooding();
  avl_for_each_element(nhdp_interface_get_tree(), flooding_data.current_interface, _node) {
    OONF_DEBUG(LOG_MPR, "Calculating flooding MPRs for interface %s",
        nhdp_interface_get_name(flooding_data.current_interface));
    
    mpr_calculate_neighbor_graph_flooding(
        nhdp_domain_get_flooding(), &flooding_data);
    mpr_calculate_mpr_rfc7181(nhdp_domain_get_flooding(),
        &flooding_data.neigh_graph);
    mpr_print_sets(&flooding_data.neigh_graph);
    _update_nhdp_flooding(&flooding_data.neigh_graph);
  }

  /* free memory */
  mpr_clear_neighbor_graph(&flooding_data.neigh_graph);
}

static void
_update_routing_mpr(void) {
  struct neighbor_graph routing_graph;
  struct nhdp_domain *domain;

  list_for_each_element(nhdp_domain_get_list(), domain, _node) {
    if (domain->mpr != &_mpr_handler) {
      /* we are not the routing MPR for this domain */
      continue;
    }
    memset(&routing_graph, 0, sizeof(routing_graph));

    mpr_calculate_neighbor_graph_routing(domain, &routing_graph);
    mpr_calculate_mpr_rfc7181(domain, &routing_graph);
    mpr_print_sets(&routing_graph);
    _update_nhdp_routing(&routing_graph);
  }
}

/**
 * Callback triggered when an MPR update is required
 */
static void
_cb_update_mpr(void) {
  OONF_DEBUG(LOG_MPR, "Recalculating MPRs");

  /* calculate flooding MPRs */
  _update_flooding_mpr();
  
  /* calculate routing MPRs */
  _update_routing_mpr();

  OONF_DEBUG(LOG_MPR, "Finished recalculating MPRs");
}

#if 0

/**
 * Validate the MPR set according to section 18.3 (draft 19)
 * @param current_mpr_data
 * @return 
 */
void
_validate_mpr_set(struct common_data *mpr_data) {
  struct n1_node *node_n1;
  struct addr_node *n2_addr;
  uint32_t d_y_n1, d_y_mpr;

  OONF_DEBUG(LOG_MPR, "Validating MPR set");

  /* 
   * First property: If x in N1 has W(x) = WILL_ALWAYS then x is in M. 
   */
  avl_for_each_element(&mpr_data->set_n1, node_n1,
      _avl_node) {
    if (node_n1->neigh->flooding_willingness
        == RFC5444_WILLINGNESS_ALWAYS) {
      assert(_is_mpr(mpr_data, &node_n1->addr));
    }
  }

  /*
   * Second property: For any y in N2 that does not have a defined d1(y), 
   * there is at least one element in M that is also in N1(y). This is 
   * equivalent to the requirement that d(y, M) is defined.
   */
  avl_for_each_element(&mpr_data->set_n2, n2_addr, _avl_node) {
    assert(_calculate_d_of_y_s(mpr_data, n2_addr, &mpr_data->set_mpr)
        != RFC5444_METRIC_INFINITE);
  }

  /*
   * Third property: For any y in N2, d(y,M) = d(y, N1).
   */
  avl_for_each_element(&mpr_data->set_n2, n2_addr, _avl_node) {
    d_y_n1 = _calculate_d_of_y_s(mpr_data, n2_addr, &mpr_data->set_n1);
    d_y_mpr = _calculate_d_of_y_s(mpr_data, n2_addr, &mpr_data->set_mpr);
    assert(d_y_n1 == d_y_mpr);
  }
}


#endif
