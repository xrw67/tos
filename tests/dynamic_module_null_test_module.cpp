#include "tos/app/module.h"

extern "C" {
TOS_DYNAMIC_MODULE_EXPORT_DECLARATION extern tos::Module* const tos_dynamic_module;
}

TOS_DYNAMIC_MODULE_EXPORT tos::Module* const tos_dynamic_module = nullptr;
