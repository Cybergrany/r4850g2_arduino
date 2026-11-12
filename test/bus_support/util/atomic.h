#pragma once
#define ATOMIC_RESTORESTATE 0
#define ATOMIC_BLOCK(kind) for (bool once=true;once;once=false)
