add_executable(eam-walkthrough EamWalkthrough.cpp)
target_link_libraries(eam-walkthrough PRIVATE ${EUCLID_CDK_DEFAULT_TARGET})
set_target_properties(eam-walkthrough PROPERTIES CXX_SCAN_FOR_MODULES OFF)

add_executable(esm-walkthrough EsmWalkthrough.cpp)
target_link_libraries(esm-walkthrough PRIVATE ${EUCLID_CDK_DEFAULT_TARGET})
set_target_properties(esm-walkthrough PROPERTIES CXX_SCAN_FOR_MODULES OFF)

add_executable(messaging-walkthrough MessagingWalkthrough.cpp)
target_link_libraries(messaging-walkthrough PRIVATE ${EUCLID_CDK_DEFAULT_TARGET})
set_target_properties(messaging-walkthrough PROPERTIES CXX_SCAN_FOR_MODULES OFF)
