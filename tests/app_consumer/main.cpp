#include "tos/app/app.h"

int main() {
    tos::App application;
    return application.state() == tos::AppState::kCreated ? 0 : 1;
}
