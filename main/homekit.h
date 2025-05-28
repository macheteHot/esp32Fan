#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void homekit_init();
void homekit_fan_state_sync(bool on, int level);
#ifdef __cplusplus
}
#endif
