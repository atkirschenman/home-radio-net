/* The node's CC1101 driver, compiled unchanged for the ESP32: both ends of
 * the link run the same code and the same generated register table. */
#include "../../../../lib/node_l0/src/cc1101.c"
/* For nl0_test_master_key (bench gateway key), with its dependency. */
#include "../../../../lib/node_l0/src/ee_ring.c"
#include "../../../../lib/node_l0/src/rnet_node.c"
