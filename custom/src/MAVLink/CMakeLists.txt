# Compatibility for the team MAVLink revision pinned in CustomOverrides.cmake.
# It renamed RELEASE/GRAB to OPEN/CLOSE and added STOP. Preserve QGC's existing
# enum type and wire values (release = 0, grab = 1) without editing src/ or the
# generated headers. Recheck these aliases when updating the pinned revision;
# enum members cannot be detected with preprocessor #ifdef checks.
add_library(CustomMAVLink INTERFACE)
target_compile_definitions(CustomMAVLink INTERFACE
    GRIPPER_ACTION_RELEASE=GRIPPER_ACTION_OPEN
    GRIPPER_ACTION_GRAB=GRIPPER_ACTION_CLOSE
)
