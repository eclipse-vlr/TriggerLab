#include "triggerlab/TrainingProfile.hpp"
#include "triggerlab/TriggerEngine.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
void check(bool value,const char* message) {if(!value) {std::cerr<<message<<'\n';std::exit(1);}}
int main() {
    using namespace triggerlab;
    auto dir=std::filesystem::temp_directory_path()/("triggerlab-profiles-"+std::to_string(Clock::now().time_since_epoch().count()));
    TrainingProfile p;p.name="Kovaak — жёлтая мишень";p.settings.dryRun=false;p.settings.side=41;p.settings.offsetX=-12;p.color.space=ColorSpace::HSV;p.color.target={0,198,215,255};p.color.hueTolerance=17.5;
    std::string error;check(saveTrainingProfile(dir,p,error),"save Unicode profile");auto list=listTrainingProfiles(dir);check(list.size()==1 && list[0].name==p.name,"list saved name");
    auto loaded=loadTrainingProfile(list[0].path);check(loaded && loaded->settings.side==41 && loaded->settings.offsetX==-12 && loaded->settings.dryRun && loaded->color.space==ColorSpace::HSV && loaded->color.hueTolerance==17.5,"complete safe roundtrip");
    p.settings.dwellMs=100;check(saveTrainingProfile(dir,p,error) && listTrainingProfiles(dir).size()==1 && loadTrainingProfile(list[0].path)->settings.dwellMs==100,"update same named profile");
    p.settings.side=200;check(!saveTrainingProfile(dir,p,error) && loadTrainingProfile(list[0].path)->settings.side==41,"invalid update preserves file");
    p.settings.side=31;p.name="../CON";check(saveTrainingProfile(dir,p,error),"path-like label safely stored");
    for(const auto& item:listTrainingProfiles(dir)) check(item.path.parent_path()==dir,"name cannot escape directory");
    p.name="bad\nname";check(!saveTrainingProfile(dir,p,error),"line injection rejected");
    {std::ofstream out(dir/"broken.tlprofile");out<<"garbage";}
    check(listTrainingProfiles(dir).size()==2 && !loadTrainingProfile(dir/"broken.tlprofile"),"corrupt profile ignored");
    {std::ofstream out(list[0].path,std::ios::app);out<<" unexpected";}
    check(!loadTrainingProfile(list[0].path),"trailing content rejected");
    std::filesystem::remove_all(dir);std::cout<<"PASS: named trainer profiles, complete settings, safe load and atomic updates\n";
}
