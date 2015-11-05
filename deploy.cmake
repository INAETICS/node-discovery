deploy("wiring" BUNDLES
   ${CELIX_BUNDLES_DIR}/shell.zip
   ${CELIX_BUNDLES_DIR}/shell_tui.zip
   org.inaetics.node_discovery.etcd.NodeDiscovery
   org.inaetics.wiring_topology_manager.WiringTopologyManager
   org.inaetics.wiring_admin.WiringAdmin
)

deploy("wiring_2" BUNDLES
   ${CELIX_BUNDLES_DIR}/shell.zip
   ${CELIX_BUNDLES_DIR}/shell_tui.zip
   org.inaetics.node_discovery.etcd.NodeDiscovery
   org.inaetics.wiring_topology_manager.WiringTopologyManager
   org.inaetics.wiring_admin.WiringAdmin
)

deploy("wiring_rsa_client" BUNDLES
   ${CELIX_BUNDLES_DIR}/shell.zip
   ${CELIX_BUNDLES_DIR}/shell_tui.zip
   ${CELIX_BUNDLES_DIR}/topology_manager.zip
#   ${CELIX_BUNDLES_DIR}/calculator_shell.zip
   org.inaetics.node_discovery.etcd.NodeDiscovery
   org.inaetics.wiring_topology_manager.WiringTopologyManager
   org.inaetics.wiring_admin.WiringAdmin
   org.inaetics.remote_service_admin
)

deploy("wiring_rsa_sever" BUNDLES
   ${CELIX_BUNDLES_DIR}/shell.zip
   ${CELIX_BUNDLES_DIR}/shell_tui.zip
   ${CELIX_BUNDLES_DIR}/topology_manager.zip
#   ${CELIX_BUNDLES_DIR}/calculator.zip
   org.inaetics.node_discovery.etcd.NodeDiscovery
   org.inaetics.wiring_topology_manager.WiringTopologyManager
   org.inaetics.wiring_admin.WiringAdmin
   org.inaetics.remote_service_admin
)

deploy("node_discovery_etcd" BUNDLES
   ${CELIX_BUNDLES_DIR}/shell.zip
   ${CELIX_BUNDLES_DIR}/shell_tui.zip
   org.inaetics.node_discovery.etcd.NodeDiscovery
)

deploy("wiring_topology_manager" BUNDLES
   ${CELIX_BUNDLES_DIR}/shell.zip
   ${CELIX_BUNDLES_DIR}/shell_tui.zip
   org.inaetics.wiring_topology_manager.WiringTopologyManager
)

deploy("wiring_admin" BUNDLES
   ${CELIX_BUNDLES_DIR}/shell.zip
   ${CELIX_BUNDLES_DIR}/shell_tui.zip
   org.inaetics.wiring_admin.WiringAdmin
)
