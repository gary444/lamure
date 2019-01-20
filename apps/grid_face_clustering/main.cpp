#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <chrono>
#include <limits>
#include <float.h>
#include <lamure/mesh/bvh.h>


#include <CGAL/Polygon_mesh_processing/measure.h>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "Utils.h"
#include "OBJ_printer.h"
#include "SymMat.h"
#include "cluster_settings.h"
#include "ErrorQuadric.h"
#include "Chart.h"
#include "JoinOperation.h"
#include "polyhedron_builder.h"
#include "eig.h"
#include "ClusterCreator.h"
#include "GridClusterCreator.h"

#include "CGAL_typedefs.h"



#define SEPARATE_CHART_FILE false




// Vector normalise(Vector v) {return v / std::sqrt(v.squared_length());}


//key: face_id, value: chart_id
std::map<uint32_t, uint32_t> chart_id_map;



// void count_faces_in_active_charts(std::vector<Chart> &charts) {
//   uint32_t active_faces = 0;
//   for (auto& chart : charts)
//   {
//     if (chart.active) 
//     {
//       active_faces += chart.facets.size();
//     }
//   }
//   std::cout << "found " << active_faces << " active faces\n";
// }



int main( int argc, char** argv ) 
{

  


  std::string obj_filename = "dino.obj";
  if (Utils::cmdOptionExists(argv, argv+argc, "-f")) {
    obj_filename = std::string(Utils::getCmdOption(argv, argv + argc, "-f"));
  }
  else {
    std::cout << "Please provide an obj filename using -f <filename.obj>" << std::endl;
    std::cout << "Optional: -ch specifies chart threshold (=100)" << std::endl;
    std::cout << "Optional: -co specifies cost threshold (=double max)" << std::endl;

    std::cout << "Optional: -ef specifies error fit coefficient (=1)" << std::endl;
    std::cout << "Optional: -eo specifies error orientation coefficient (=1)" << std::endl;
    std::cout << "Optional: -es specifies error shape coefficient (=1)" << std::endl;
    return 1;
  }

  double cost_threshold = std::numeric_limits<double>::max();
  if (Utils::cmdOptionExists(argv, argv+argc, "-co")) {
    cost_threshold = atof(Utils::getCmdOption(argv, argv + argc, "-co"));
  }
  uint32_t chart_threshold = 100;
  if (Utils::cmdOptionExists(argv, argv+argc, "-ch")) {
    chart_threshold = atoi(Utils::getCmdOption(argv, argv + argc, "-ch"));
  }
  double e_fit_cf = 1.0;
  if (Utils::cmdOptionExists(argv, argv+argc, "-ef")) {
    e_fit_cf = atof(Utils::getCmdOption(argv, argv + argc, "-ef"));
  }
  double e_ori_cf = 1.0;
  if (Utils::cmdOptionExists(argv, argv+argc, "-eo")) {
    e_ori_cf = atof(Utils::getCmdOption(argv, argv + argc, "-eo"));
  }
  double e_shape_cf = 1.0;
  if (Utils::cmdOptionExists(argv, argv+argc, "-es")) {
    e_shape_cf = atof(Utils::getCmdOption(argv, argv + argc, "-es"));
  }
  CLUSTER_SETTINGS cluster_settings (e_fit_cf, e_ori_cf, e_shape_cf);


  std::vector<lamure::mesh::triangle_t> triangles;

    //load OBJ into arrays
  std::vector<double> vertices;
  std::vector<int> tris;
  std::vector<double> t_coords;
  std::vector<int> tindices;
  BoundingBoxLimits limits = Utils::load_obj( obj_filename, vertices, tris, t_coords, tindices, triangles);





  if (vertices.size() == 0 ) {
    std::cout << "didnt find any vertices" << std::endl;
    return 1;
  }
  std::cout << "Mesh loaded (" << triangles.size() << " triangles, " << vertices.size() / 3 << " vertices, " << tris.size() / 3 << " faces, " << t_coords.size() / 2 << " tex coords)" << std::endl;

  
  //START chart creation ====================================================================================================================
  auto start_time = std::chrono::system_clock::now();



  // build a polyhedron from the loaded arrays
  Polyhedron polyMesh;
  bool check_vertices = true;
  polyhedron_builder<HalfedgeDS> builder( vertices, tris, t_coords, tindices, check_vertices );


  polyMesh.delegate( builder );

  //give ids to polyMesh
  // int i=0;
  // for ( Facet_iterator fb = polyMesh.facets_begin(); fb != polyMesh.facets_end(); ++fb){
  //   // fb->id() = i++;  
  //   std::cout << "Face id: " << fb->id() << std::endl;
  // }



  if (polyMesh.is_valid(false)){
    std::cout << "mesh valid\n"; 
  }


  if (!CGAL::is_triangle_mesh(polyMesh)){
    std::cerr << "Input geometry is not triangulated." << std::endl;
    return EXIT_FAILURE;
  }
  else {
    std::cout << "mesh is triangulated\n";
  }



  //TODO rewrite to derive charts from CGAL face ids

  uint32_t active_charts = GridClusterCreator::create_grid_clusters(polyMesh,chart_id_map, limits);

  std::cout << "Grid clusters: " << active_charts << std::endl;

  active_charts = ClusterCreator::create_chart_clusters_from_grid_clusters(polyMesh,cost_threshold, chart_threshold, cluster_settings, chart_id_map, active_charts);

  std::cout << "After creating chart clusters: " << active_charts << std::endl;

  // ClusterCreator::create_chart_clusters_from_faces(polyMesh, cost_threshold, chart_threshold, cluster_settings, chart_id_map);


  //END chart creation ====================================================================================================================

  std::string out_filename = "data/charts1.obj";
  std::ofstream ofs( out_filename );
  OBJ_printer::print_polyhedron_wavefront_with_charts( ofs, polyMesh,chart_id_map, active_charts, SEPARATE_CHART_FILE);
  ofs.close();
  std::cout << "simplified mesh was written to " << out_filename << std::endl;

  //Logging
  auto time = std::chrono::system_clock::now();
  std::chrono::duration<double> diff = time - start_time;
  std::time_t now_c = std::chrono::system_clock::to_time_t(time);
  std::string log_path = "../../data/logs/chart_creation_log.txt";
  ofs.open (log_path, std::ofstream::out | std::ofstream::app);
  ofs << "\n-------------------------------------\n";
  
  ofs << "Executed at " << std::put_time(std::localtime(&now_c), "%F %T") << std::endl;
  ofs << "Ran for " << (int)diff.count() / 60 << " m "<< (int)diff.count() % 60 << " s" << std::endl;
  ofs << "Input file: " << obj_filename << "\nOutput file: " << out_filename << std::endl;
  ofs << "Vertices: " << vertices.size()/3 << " , faces: " << tris.size()/3 << std::endl; 
  ofs << "Desired Charts: " << chart_threshold << ", active charts: " << active_charts << std::endl;
  ofs << "Cost threshold: " << cost_threshold << std::endl;
  ofs << "Cluster settings: e_fit: " << cluster_settings.e_fit_cf << ", e_ori: " << cluster_settings.e_ori_cf << ", e_shape" << cluster_settings.e_shape_cf << std::endl;

    ofs.close();
  std::cout << "Log written to " << log_path << std::endl;



  return EXIT_SUCCESS ; 
}