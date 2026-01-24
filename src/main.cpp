#include "app.h"

#include <string>

int main(int argc, char** argv) {
    std::string config_path = "config.ini";
    if (argc > 1 && argv[1] && argv[1][0] != '\0') {
        config_path = argv[1];
    }
    App app;
    return app.run(config_path);
}
