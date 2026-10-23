#include <iostream>
#include <vr/Viewer.h>

// Starts the program and returns a failing status when an exception escapes.
int main(int argc, char** argv){
    try{
        vr::Viewer viewer;
        return viewer.run(argc, argv);
    } catch (const std::exception& error){
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
