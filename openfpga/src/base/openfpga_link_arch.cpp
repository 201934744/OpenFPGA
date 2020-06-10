/********************************************************************
 * This file includes functions to read an OpenFPGA architecture file
 * which are built on the libarchopenfpga library
 *******************************************************************/

/* Headers from vtrutil library */
#include "vtr_time.h"
#include "vtr_assert.h"
#include "vtr_log.h"

/* Headers from openfpgashell library */
#include "command_exit_codes.h"

/* Headers from vpr library */
#include "read_activity.h"

#include "vpr_device_annotation.h"
#include "pb_type_utils.h"
#include "annotate_pb_types.h"
#include "annotate_pb_graph.h"
#include "annotate_routing.h"
#include "annotate_rr_graph.h"
#include "annotate_simulation_setting.h"
#include "mux_library_builder.h"
#include "build_tile_direct.h"
#include "annotate_placement.h"
#include "openfpga_link_arch.h"

/* Include global variables of VPR */
#include "globals.h"

/* begin namespace openfpga */
namespace openfpga {

/********************************************************************
 * A function to identify if the routing resource graph generated by
 * VPR is support by OpenFPGA
 * - Currently we only support uni-directional
 *   It means every routing tracks must have a direction
 *******************************************************************/
static 
bool is_vpr_rr_graph_supported(const RRGraph& rr_graph) {
  /* Check if the rr_graph is uni-directional*/
  for (const RRNodeId& node : rr_graph.nodes()) {
    if (CHANX != rr_graph.node_type(node) && CHANY != rr_graph.node_type(node)) {
      continue;
    }
    if (BI_DIRECTION == rr_graph.node_direction(node)) {
      VTR_LOG_ERROR("Routing resource graph is bi-directional. OpenFPGA currently supports uni-directional routing architecture only.\n");
      return false;
    }
  }
 
  return true;
}

/********************************************************************
 * Top-level function to link openfpga architecture to VPR, including:
 * - physical pb_type
 * - mode selection bits for pb_type and pb interconnect
 * - circuit models for pb_type and pb interconnect
 * - physical pb_graph nodes and pb_graph pins
 * - circuit models for global routing architecture
 *******************************************************************/
int link_arch(OpenfpgaContext& openfpga_ctx,
              const Command& cmd, const CommandContext& cmd_context) { 

  vtr::ScopedStartFinishTimer timer("Link OpenFPGA architecture to VPR architecture");

  CommandOptionId opt_activity_file = cmd.option("activity_file");
  CommandOptionId opt_sort_edge = cmd.option("sort_gsb_chan_node_in_edges");
  CommandOptionId opt_verbose = cmd.option("verbose");

  /* Annotate pb_type graphs
   * - physical pb_type
   * - mode selection bits for pb_type and pb interconnect
   * - circuit models for pb_type and pb interconnect
   */
  annotate_pb_types(g_vpr_ctx.device(), openfpga_ctx.arch(),
                    openfpga_ctx.mutable_vpr_device_annotation(),
                    cmd_context.option_enable(cmd, opt_verbose));

  /* Annotate pb_graph_nodes
   * - Give unique index to each node in the same type
   * - Bind operating pb_graph_node to their physical pb_graph_node
   * - Bind pins from operating pb_graph_node to their physical pb_graph_node pins
   */
  annotate_pb_graph(g_vpr_ctx.device(),
                    openfpga_ctx.mutable_vpr_device_annotation(),
                    cmd_context.option_enable(cmd, opt_verbose));

  /* Annotate routing architecture to circuit library */
  annotate_rr_graph_circuit_models(g_vpr_ctx.device(),
                                   openfpga_ctx.arch(),
                                   openfpga_ctx.mutable_vpr_device_annotation(),
                                   cmd_context.option_enable(cmd, opt_verbose));

  /* Annotate net mapping to each rr_node 
   */
  openfpga_ctx.mutable_vpr_routing_annotation().init(g_vpr_ctx.device().rr_graph);

  annotate_rr_node_nets(g_vpr_ctx.device(), g_vpr_ctx.clustering(), g_vpr_ctx.routing(), 
                        openfpga_ctx.mutable_vpr_routing_annotation(),
                        cmd_context.option_enable(cmd, opt_verbose));

  /* Build the routing graph annotation
   * - RRGSB
   * - DeviceRRGSB 
   */
  if (false == is_vpr_rr_graph_supported(g_vpr_ctx.device().rr_graph)) {
    return CMD_EXEC_FATAL_ERROR;
  }

  annotate_device_rr_gsb(g_vpr_ctx.device(),
                         openfpga_ctx.mutable_device_rr_gsb(),
                         cmd_context.option_enable(cmd, opt_verbose));

  if (true == cmd_context.option_enable(cmd, opt_sort_edge)) {
    sort_device_rr_gsb_chan_node_in_edges(g_vpr_ctx.device().rr_graph,
                                          openfpga_ctx.mutable_device_rr_gsb());
  } 

  /* Build multiplexer library */
  openfpga_ctx.mutable_mux_lib() = build_device_mux_library(g_vpr_ctx.device(),
                                                            const_cast<const OpenfpgaContext&>(openfpga_ctx)); 

  /* Build tile direct annotation */
  openfpga_ctx.mutable_tile_direct() = build_device_tile_direct(g_vpr_ctx.device(),
                                                                openfpga_ctx.arch().arch_direct,
                                                                cmd_context.option_enable(cmd, opt_verbose));

  /* Annotate placement results */
  annotate_mapped_blocks(g_vpr_ctx.device(), 
                         g_vpr_ctx.clustering(),
                         g_vpr_ctx.placement(),
                         openfpga_ctx.mutable_vpr_placement_annotation());

  /* Read activity file is manadatory in the following flow-run settings
   * - When users specify that number of clock cycles 
   *   should be inferred from FPGA implmentation
   * - When FPGA-SPICE is enabled
   */
  openfpga_ctx.mutable_net_activity() = read_activity(g_vpr_ctx.atom().nlist,
                                                      cmd_context.option_value(cmd, opt_activity_file).c_str());

  /* TODO: Annotate the number of clock cycles and clock frequency by following VPR results
   * We SHOULD create a new simulation setting for OpenFPGA use only
   * Avoid overwrite the raw data achieved when parsing!!!
   */
  /* OVERWRITE the simulation setting in openfpga context from the arch
   * TODO: This will be removed when openfpga flow is updated  
   */
  openfpga_ctx.mutable_simulation_setting() = openfpga_ctx.mutable_arch().sim_setting;
  annotate_simulation_setting(g_vpr_ctx.atom(),
                              openfpga_ctx.net_activity(),
                              openfpga_ctx.mutable_simulation_setting());

  /* TODO: should identify the error code from internal function execution */
  return CMD_EXEC_SUCCESS;
} 

} /* end namespace openfpga */
