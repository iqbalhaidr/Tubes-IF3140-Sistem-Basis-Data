#include <iostream>
#include <memory>

#include "storage_manager.h"
#include "query_optimizer.h"
#include "query_processor.h"
#include "tui.h"

int main() {
    mdbms::tui::TUI tui;
    tui.run();
    
    return 0;
}
