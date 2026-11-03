#include "GyroscopeAnimation.h"
#include "RoomIsolation.h"
#include <vr/Viewer.h>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv){
    pantheon::registerGyroscopes();
    pantheon::registerRoomIsolation();
    std::vector<std::string> arguments(argv,argv+argc);
    if(const auto view=std::find(arguments.begin(),arguments.end(),"--view");view!=arguments.end()){
        if(view+1==arguments.end() || (view+1)->empty() ||
           !std::all_of((view+1)->begin(),(view+1)->end(),[](unsigned char c){return std::isalnum(c) || c=='-';}) ||
           std::find(arguments.begin(),arguments.end(),"--scene")!=arguments.end()){
            std::cerr<<"--view requires one view name and cannot be combined with --scene\n";
            return 2;
        }
        const auto scene="experiments/pantheon/views/"+*(view+1)+".xml";
        arguments.erase(view,view+2);
        arguments.push_back("--scene");arguments.push_back(scene);
    }
    if(std::find(arguments.begin(),arguments.end(),"--scene")==arguments.end()){
        arguments.push_back("--scene");
        arguments.push_back("experiments/pantheon/scene.xml");
    }
    std::vector<char*> values;
    for(auto& argument:arguments)values.push_back(argument.data());
    return vr::Viewer().run(static_cast<int>(values.size()),values.data());
}
