#ifndef OPENCNC3D_BUILDING_SERVICE_TYPES_H
#define OPENCNC3D_BUILDING_SERVICE_TYPES_H
struct BServiceMesh {
    int source=-1, body=-1, part[6]={-1,-1,-1,-1,-1,-1};
    std::vector<int> bp, pp;
};
struct BService {
    int kind=0;
    std::map<int,BServiceMesh> meshes;
    std::map<int,std::vector<int> > tex;
};
#endif
