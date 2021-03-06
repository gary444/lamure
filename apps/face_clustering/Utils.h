#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>


#include "CGAL_typedefs.h"

#ifndef UTILSH
#define UTILSH


struct Utils
{
	static char* getCmdOption(char ** begin, char ** end, const std::string & option) {
	  char ** itr = std::find(begin, end, option);
	  if (itr != end && ++itr != end) {
	      return *itr;
	  }
	  return 0;
	}

	static bool cmdOptionExists(char** begin, char** end, const std::string& option) {
	  return std::find(begin, end, option) != end;
	}

	//parses a face string like "f  2//1  8//1  4//1 " into 3 given arrays
	static void parse_face_string (std::string face_string, int (&index)[3], int (&coord)[3], int (&normal)[3]){

	  //split by space into faces
	  std::vector<std::string> faces;
	  boost::algorithm::trim(face_string);
	  boost::algorithm::split(faces, face_string, boost::algorithm::is_any_of(" "), boost::algorithm::token_compress_on);

	  for (int i = 0; i < 3; ++i)
	  {
	    //split by / for indices
	    std::vector<std::string> inds;
	    boost::algorithm::split(inds, faces[i], [](char c){return c == '/';}, boost::algorithm::token_compress_off);

	    for (int j = 0; j < (int)inds.size(); ++j)
	    {
	      int idx = 0;
	      //parse value from string
	      if (inds[j] != ""){
	        idx = stoi(inds[j]);
	      }
	      if (j == 0){index[i] = idx;}
	      else if (j == 1){coord[i] = idx;}
	      else if (j == 2){normal[i] = idx;}
	      
	    }
	  }
	}

	static Vector normalise(Vector v) {return v / std::sqrt(v.squared_length());}

	// load obj function from vt_obj_loader/Utils.h
	static void load_obj(const std::string& filename, 
	              std::vector<double> &v, 
	              std::vector<int> &vindices, 
	              std::vector<double> &t,
	              std::vector<int> &tindices ){


	  FILE *file = fopen(filename.c_str(), "r");

	  if (0 != file) {

	    while (true) {
	      char line[128];
	      int32_t l = fscanf(file, "%s", line);

	      if (l == EOF) break;
	      if (strcmp(line, "v") == 0) {
	        double vx, vy, vz;
	        fscanf(file, "%lf %lf %lf\n", &vx, &vy, &vz);
	        v.insert(v.end(), {vx, vy, vz});
	      } 
	      // else if (strcmp(line, "vn") == 0) {
	      //   float nx, ny, nz;
	      //   fscanf(file, "%f %f %f\n", &nx, &ny, &nz);
	      //   n.insert(n.end(), {nx,ny, nz});
	      // } 
	      else if (strcmp(line, "vt") == 0) {
	        double tx, ty;
	        fscanf(file, "%lf %lf\n", &tx, &ty);
	        t.insert(t.end(), {tx, ty});
	      } 
	      else if (strcmp(line, "f") == 0) {
	        fgets(line, 128, file);
	        std::string face_string = line; 
	        int index[3];
	        int coord[3];
	        int normal[3];

	        parse_face_string(face_string, index, coord, normal);

	        //here all indices are decremented by 1 to fit 0 indexing schemes
	        vindices.insert(vindices.end(), {index[0]-1, index[1]-1, index[2]-1});
	        tindices.insert(tindices.end(), {coord[0]-1, coord[1]-1, coord[2]-1});
	        // nindices.insert(nindices.end(), {normal[0]-1, normal[1]-1, normal[2]-1});
	      }
	    }

	    fclose(file);

	    std::cout << "positions: " << v.size()/3 << std::endl;
	    // std::cout << "normals: " << n.size()/3 << std::endl;
	    std::cout << "coords: " << t.size()/2 << std::endl;
	    std::cout << "faces: " << vindices.size()/3 << std::endl;

	  }

	}

};

#endif