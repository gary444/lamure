// Copyright (c) 2014-2018 Bauhaus-Universitaet Weimar
// This Software is distributed under the Modified BSD License, see license.txt.
//
// Virtual Reality and Visualization Research Group 
// Faculty of Media, Bauhaus-Universitaet Weimar
// http://www.uni-weimar.de/medien/vr

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <set>
#include <map>

#include <lamure/types.h>
#include <lamure/ren/dataset.h>
#include <lamure/ren/bvh.h>
#include <lamure/ren/lod_stream.h>

#include "lodepng.h"
#include "texture.h"
#include "frame_buffer.h"
#include "Utils.h"


struct vertex {
  scm::math::vec3f pos_;
  scm::math::vec3f nml_;
  scm::math::vec2f old_coord_;
};

struct triangle {
  vertex v0_;
  vertex v1_;
  vertex v2_;
  int tex_id;
};

struct rectangle {
  scm::math::vec2f min_;
  scm::math::vec2f max_;
  int id_;
  bool flipped_;
};


struct projection_info {
  scm::math::vec3f proj_centroid;
  scm::math::vec3f proj_normal;
  scm::math::vec3f proj_world_up;

  rectangle tex_space_rect;
  scm::math::vec2f tex_coord_offset;

  float largest_max;
};

struct chart {
  int id_;
  rectangle rect_;
  lamure::bounding_box box_;
  std::set<int> all_triangle_ids_;
  std::set<int> original_triangle_ids_;
  std::map<int, std::vector<scm::math::vec2f>> all_triangle_new_coods_;
  projection_info projection;
  double real_to_tex_ratio_old;
  double real_to_tex_ratio_new;
};

struct blit_vertex {
  scm::math::vec2f old_coord_;
  scm::math::vec2f new_coord_;
};

struct viewport {
  scm::math::vec2f normed_dims;
  scm::math::vec2f normed_offset;
};


//global rendering variables:

int window_width_ = 1024;
int window_height_ = 1024;

int elapsed_ms_ = 0;
int num_vertices_ = 0;


std::vector<std::string> texture_paths;

std::vector<blit_vertex> to_upload;
std::vector<std::vector<blit_vertex> > to_upload_per_texture;


GLuint shader_program_; //contains GPU-code
GLuint vertex_buffer_; //contains 3d model

std::vector< std::shared_ptr<texture_t> > textures_;

std::shared_ptr<frame_buffer_t> frame_buffer_; //contains resulting image

std::string outfile_name = "tex_out.png";

std::vector<viewport> viewports;






char* get_cmd_option(char** begin, char** end, const std::string & option) {
    char** it = std::find(begin, end, option);
    if (it != end && ++it != end)
        return *it;
    return 0;
}

bool cmd_option_exists(char** begin, char** end, const std::string& option) {
    return std::find(begin, end, option) != end;
}


int load_chart_file(std::string chart_file, std::vector<int>& chart_id_per_triangle) {

  int num_charts = 0;

  std::ifstream file(chart_file);

  std::string line;
  while (std::getline(file, line)) {
    std::stringstream ss(line);
    std::string chart_id_str;

    while (std::getline(ss, chart_id_str, ' ')) {
      
      int chart_id = atoi(chart_id_str.c_str());
      num_charts = std::max(num_charts, chart_id+1);
      chart_id_per_triangle.push_back(chart_id);
    }
  }


  file.close();

  return num_charts;
}


//reads an ".lodtexid" file into a vector, returns number of textures (highest texture id + 1)
// if ".lodtexid" file is not found, returns 0
int load_tex_id_file(std::string tex_id_file_name, std::vector<int>& tex_id_per_triangle){

  int num_textures = 0;

  std::ifstream file(tex_id_file_name);

  if(!file.good()){
    std::cout << "No texture ID file was found ( searched for " << tex_id_file_name << ")\n";
    return 0;
  }
  std::cout << "Found texture ids in file: " << tex_id_file_name << std::endl;

  std::string line;
  while (std::getline(file, line)) {
    std::stringstream ss(line);
    std::string tex_id_str;

    while (std::getline(ss, tex_id_str, ' ')) {
      
      int tex_id = atoi(tex_id_str.c_str());
      num_textures = std::max(num_textures, tex_id+1);
      tex_id_per_triangle.push_back(tex_id);
    }
  }

  file.close();
  return num_textures;
}


//gets texture ids from file
//applies texture ids to triangle vector
// returns number of textures found (if multiple textures exist) or 0 if no texture lod file is found
int 
load_tex_ids(std::string tex_id_file_name, std::vector<triangle>& triangles, int first_leaf_tri_offset){
    
    std::vector<int> tex_id_per_triangle;
    int num_textures = load_tex_id_file(tex_id_file_name, tex_id_per_triangle);
    std::cout << "Found " << num_textures << " input textures" << std::endl;
    //apply texture ids to triangles
    if (tex_id_per_triangle.size() > 0)
    {
      std::cout << "Attempting to apply tex ids to triangles..." << std::endl;
      if (tex_id_per_triangle.size() != (triangles.size() - first_leaf_tri_offset))
      {
        std::cout << "Warning: some triangles did not have texture ids. They will be rendered from the first texture loaded\n";

        std::cout << "Leaf level triangles: " << triangles.size() - first_leaf_tri_offset << " input tex ids: " << tex_id_per_triangle.size() << std::endl;
      }
      for (uint32_t i = 0; i < std::min(tex_id_per_triangle.size() - first_leaf_tri_offset, triangles.size()); ++i){
        triangles[i].tex_id = tex_id_per_triangle[i + first_leaf_tri_offset];
      }
    }

    return num_textures;
}



std::shared_ptr<texture_t> load_image(const std::string& filepath) {
  std::vector<unsigned char> img;
  unsigned int width = 0;
  unsigned int height = 0;
  int tex_error = lodepng::decode(img, width, height, filepath);
  if (tex_error) {
    std::cout << "unable to load image file " << filepath << std::endl;
  }
  else {std::cout << "image " << filepath << " loaded" << std::endl;}

  auto texture = std::make_shared<texture_t>(width, height, GL_LINEAR);
  texture->set_pixels(&img[0]);

  return texture;
}

void load_textures(){

  std::cout << "Loading all textures" << std::endl;
  for (auto tex_path : texture_paths)
  {
    textures_.push_back(load_image(tex_path));
  }
}


scm::math::vec2f project_to_plane(
  const scm::math::vec3f& v, scm::math::vec3f& plane_normal, 
  const scm::math::vec3f& centroid,
  const scm::math::vec3f& world_up
  ) {

  scm::math::vec3f v_minus_p(
    v.x - centroid.x,
    v.y - centroid.y,
    v.z - centroid.z);

  auto plane_right = scm::math::cross(plane_normal, world_up);
  plane_right = scm::math::normalize(plane_right);
  auto plane_up = scm::math::cross(plane_normal, plane_right);
  plane_up = scm::math::normalize(plane_up);


  //project vertices to the plane
  scm::math::vec2f projected_v(
    scm::math::dot(v_minus_p, plane_right),
    scm::math::dot(v_minus_p, plane_up));

  return projected_v;

}

void project(std::vector<chart>& charts, std::vector<triangle>& triangles) {

  //keep a record of the largest chart edge
  float largest_max = 0.f;


  std::cout << "Projecting to plane:\n";

  //iterate all charts
  for (uint32_t chart_id = 0; chart_id < charts.size(); ++chart_id) {
    chart& chart = charts[chart_id];

    scm::math::vec3f avg_normal(0.f, 0.f, 0.f);
    scm::math::vec3f centroid(0.f, 0.f, 0.f);

    // compute average normal and centroid
    for (auto tri_id : chart.original_triangle_ids_) {

      triangle& tri = triangles[tri_id];

      avg_normal.x += tri.v0_.nml_.x;
      avg_normal.y += tri.v0_.nml_.y;
      avg_normal.z += tri.v0_.nml_.z;

      avg_normal.x += tri.v1_.nml_.x;
      avg_normal.y += tri.v1_.nml_.y;
      avg_normal.z += tri.v1_.nml_.z;

      avg_normal.x += tri.v2_.nml_.x;
      avg_normal.y += tri.v2_.nml_.y;
      avg_normal.z += tri.v2_.nml_.z;

      centroid.x += tri.v0_.pos_.x;
      centroid.y += tri.v0_.pos_.y;
      centroid.z += tri.v0_.pos_.z;

      centroid.x += tri.v1_.pos_.x;
      centroid.y += tri.v1_.pos_.y;
      centroid.z += tri.v1_.pos_.z;

      centroid.x += tri.v2_.pos_.x;
      centroid.y += tri.v2_.pos_.y;
      centroid.z += tri.v2_.pos_.z;
    }

    avg_normal.x /= (float)(chart.original_triangle_ids_.size()*3);
    avg_normal.y /= (float)(chart.original_triangle_ids_.size()*3);
    avg_normal.z /= (float)(chart.original_triangle_ids_.size()*3);

    centroid.x /= (float)(chart.original_triangle_ids_.size()*3);
    centroid.y /= (float)(chart.original_triangle_ids_.size()*3);
    centroid.z /= (float)(chart.original_triangle_ids_.size()*3);

    // std::cout << "chart id " << chart_id << ", num tris: " << chart.all_triangle_ids_.size() << std::endl;
    // std::cout << "  avg_n: (" << avg_normal.x << " " << avg_normal.y << " " << avg_normal.z << ")" << std::endl;
    // std::cout << "  centr: (" << centroid.x << " " << centroid.y << " " << centroid.z << ")" << std::endl;

    avg_normal = scm::math::normalize(avg_normal);
    
    //compute world up vector
    scm::math::vec3f world_up(0.f, 1.f, 0.f);
    if (std::abs(scm::math::dot(world_up, avg_normal)) > 0.8f) {
      world_up = scm::math::vec3f(0.f, 0.f, 1.f);
    }

    //record centroid, projection normal and world up for calculating inner triangle UVs later on
    chart.projection.proj_centroid = centroid;
    chart.projection.proj_normal = avg_normal;
    chart.projection.proj_world_up = world_up;


    //project all vertices into that plane
    for (auto tri_id : chart.all_triangle_ids_) {

      triangle& tri = triangles[tri_id];

      scm::math::vec2f projected_v0 = project_to_plane(tri.v0_.pos_, avg_normal, centroid, world_up);
      scm::math::vec2f projected_v1 = project_to_plane(tri.v1_.pos_, avg_normal, centroid, world_up);
      scm::math::vec2f projected_v2 = project_to_plane(tri.v2_.pos_, avg_normal, centroid, world_up);

      
      std::vector<scm::math::vec2f> coords = {
        projected_v0, projected_v1, projected_v2};
      chart.all_triangle_new_coods_.insert(std::make_pair(tri_id, coords));

      // std::cout << "tri_id " << tri_id << " projected v0: (" << projected_v0.x << " " << projected_v0.y << ")" << std::endl;
      // std::cout << "tri_id " << tri_id << " projected v1: (" << projected_v1.x << " " << projected_v1.y << ")" << std::endl;
      // std::cout << "tri_id " << tri_id << " projected v2: (" << projected_v2.x << " " << projected_v2.y << ")" << std::endl;

    }

    //compute rectangle for the current chart
    //initialize rectangle min and max
    chart.rect_.id_ = chart.id_;
    chart.rect_.flipped_ = false;
    chart.rect_.min_.x = std::numeric_limits<float>::max();
    chart.rect_.min_.y = std::numeric_limits<float>::max();
    chart.rect_.max_.x = std::numeric_limits<float>::lowest();
    chart.rect_.max_.y = std::numeric_limits<float>::lowest();

    //compute the bounding rectangle for each chart
    for (auto tri_id : chart.all_triangle_ids_) {
      triangle& tri = triangles[tri_id];

      chart.rect_.min_.x = std::min(chart.rect_.min_.x, chart.all_triangle_new_coods_[tri_id][0].x);
      chart.rect_.min_.y = std::min(chart.rect_.min_.y, chart.all_triangle_new_coods_[tri_id][0].y);
      chart.rect_.min_.x = std::min(chart.rect_.min_.x, chart.all_triangle_new_coods_[tri_id][1].x);
      chart.rect_.min_.y = std::min(chart.rect_.min_.y, chart.all_triangle_new_coods_[tri_id][1].y);
      chart.rect_.min_.x = std::min(chart.rect_.min_.x, chart.all_triangle_new_coods_[tri_id][2].x);
      chart.rect_.min_.y = std::min(chart.rect_.min_.y, chart.all_triangle_new_coods_[tri_id][2].y);

      chart.rect_.max_.x = std::max(chart.rect_.max_.x, chart.all_triangle_new_coods_[tri_id][0].x);
      chart.rect_.max_.y = std::max(chart.rect_.max_.y, chart.all_triangle_new_coods_[tri_id][0].y);
      chart.rect_.max_.x = std::max(chart.rect_.max_.x, chart.all_triangle_new_coods_[tri_id][1].x);
      chart.rect_.max_.y = std::max(chart.rect_.max_.y, chart.all_triangle_new_coods_[tri_id][1].y);
      chart.rect_.max_.x = std::max(chart.rect_.max_.x, chart.all_triangle_new_coods_[tri_id][2].x);
      chart.rect_.max_.y = std::max(chart.rect_.max_.y, chart.all_triangle_new_coods_[tri_id][2].y);
    }

    //record offset for rendering from texture
    chart.projection.tex_coord_offset.x = chart.rect_.min_.x;
    chart.projection.tex_coord_offset.y = chart.rect_.min_.y;

    //shift min to the origin
    chart.rect_.max_.x -= chart.rect_.min_.x;
    chart.rect_.max_.y -= chart.rect_.min_.y;

    //update largest chart
    largest_max = std::max(largest_max, chart.rect_.max_.x);
    largest_max = std::max(largest_max, chart.rect_.max_.y);




    // shift projected coordinates to min = 0
    for (auto tri_id : chart.all_triangle_ids_) {
      triangle& tri = triangles[tri_id];

      chart.all_triangle_new_coods_[tri_id][0].x -= chart.rect_.min_.x;
      chart.all_triangle_new_coods_[tri_id][0].y -= chart.rect_.min_.y;
      chart.all_triangle_new_coods_[tri_id][1].x -= chart.rect_.min_.x;
      chart.all_triangle_new_coods_[tri_id][1].y -= chart.rect_.min_.y;
      chart.all_triangle_new_coods_[tri_id][2].x -= chart.rect_.min_.x;
      chart.all_triangle_new_coods_[tri_id][2].y -= chart.rect_.min_.y;


    }

    chart.rect_.min_.x = 0.f;
    chart.rect_.min_.y = 0.f;

    


  }

  std::cout << "normalize charts but keep relative size:\n";
  for (int chart_id = 0; chart_id < charts.size(); ++chart_id) {
    chart& chart = charts[chart_id];
    
    // normalize largest_max to 1
    for (auto tri_id : chart.all_triangle_ids_) {
      //triangle& tri = triangles[tri_id];

      chart.all_triangle_new_coods_[tri_id][0].x /= largest_max;
      chart.all_triangle_new_coods_[tri_id][0].y /= largest_max;
      chart.all_triangle_new_coods_[tri_id][1].x /= largest_max;
      chart.all_triangle_new_coods_[tri_id][1].y /= largest_max;
      chart.all_triangle_new_coods_[tri_id][2].x /= largest_max;
      chart.all_triangle_new_coods_[tri_id][2].y /= largest_max;


    }

    chart.rect_.max_.x /= largest_max;
    chart.rect_.max_.y /= largest_max;

    chart.projection.largest_max = largest_max;

     std::cout << "chart " << chart_id << " rect min: " << chart.rect_.min_.x << ", " << chart.rect_.min_.y << std::endl;
     std::cout << "chart " << chart_id << " rect max: " << chart.rect_.max_.x << ", " << chart.rect_.max_.y << std::endl;

  }
}



void save_image(std::string filename, std::vector<uint8_t> image, int width, int height) {
  int tex_error = lodepng::encode(filename, image, width, height);
  if (tex_error) {
    std::cout << "unable to save image file " << filename << std::endl;
  }
  std::cout << "image " << filename << " saved" << std::endl;

}


void save_image(std::string filename, std::shared_ptr<frame_buffer_t> frame_buffer) {

  std::vector<uint8_t> pixels;
  frame_buffer->get_pixels(0, pixels);

  int tex_error = lodepng::encode(filename, pixels, frame_buffer->get_width(), frame_buffer->get_height());
  if (tex_error) {
    std::cout << "unable to save image file " << filename << std::endl;
  }
  std::cout << "image " << filename << " saved" << std::endl;

}


//comparison function to sort the rectangles by height 
bool sort_by_height (rectangle i, rectangle j){
  bool i_smaller_j = false;
  float height_of_i = i.max_.y-i.min_.y;
  float height_of_j = j.max_.y-j.min_.y;
  if (height_of_i > height_of_j) {
    i_smaller_j = true;
  }
  return i_smaller_j;
}

rectangle pack(std::vector<rectangle>& input, float scale_factor = 0.9f) {

  if (input.size() == 0)
  {
    std::cout << "WARNING: no rectangles received in pack() function" << std::endl; 
    return rectangle {scm::math::vec2f(0,0), scm::math::vec2f(0,0),1,0};
  }

  std::vector<rectangle> starting_rects = input;

  //make sure all rectangles stand on the shorter side
  for(uint32_t i=0; i< input.size(); i++){
    auto& rect=input[i];
    if ((rect.max_.x-rect.min_.x) > (rect.max_.y - rect.min_.y)){
      float temp = rect.max_.y;
      rect.max_.y=rect.max_.x;
      rect.max_.x=temp;
      rect.flipped_ = true;
    }
  }

  //sort by height
  std::sort (input.begin(),input.end(),sort_by_height);

  //calc the size of the texture
  float dim = sqrtf(input.size());
  std::cout << dim << " -> " << std::ceil(dim) << std::endl;
  dim = std::ceil(dim);


  //get the largest rect
  std::cout << input[0].max_.y-input[0].min_.y << std::endl;
  float max_height = input[0].max_.y-input[0].min_.y;

  //compute the average height of all rects
  float sum = 0.f;
  for (uint32_t i=0; i<input.size(); i++){

    float height = input[i].max_.y-input[i].min_.y;
    if (height < 0){
      std::cout << "WARNING: rect [" << i << "] invalid height in pack(): " << height << std::endl;
      //TODO remove offending rectangle?
      continue;
    }
    sum+= (height); 
  }

  float average_height = sum/((float)input.size());


  if (average_height <= 0)
  {
    std::cout << "WARNING: average height error" << std::endl; 
    return rectangle {scm::math::vec2f(0,0), scm::math::vec2f(0,0),1,0};
  }

  //heuristically take half
  // dim *= 0.9f;
  dim *= scale_factor;
  

  // rectangle texture{scm::math::vec2f(0,0), scm::math::vec2f((int)((dim+1)*average_height),(int)((dim+1)*average_height)),0};

  rectangle texture{scm::math::vec2f(0,0), scm::math::vec2f((int)((dim)*average_height),(int)((dim)*average_height)),0};

  std::cout << "texture size: " << texture.max_.x << " x " << texture.max_.y<< std::endl;

  
  //pack the rects
  int offset_x =0;
  int offset_y =0;
  float highest_of_current_line = input[0].max_.y-input[0].min_.y;
  for(int i=0; i< input.size(); i++){
    auto& rect = input[i];
    if ((offset_x+ (rect.max_.x - rect.min_.x)) > texture.max_.x){

      offset_y += highest_of_current_line;
      offset_x =0;
      highest_of_current_line = rect.max_.y - rect.min_.y;
      
    }
    
    rect.max_.x= offset_x + (rect.max_.x - rect.min_.x);
    rect.min_.x= offset_x;
    offset_x+= rect.max_.x - rect.min_.x;

    rect.max_.y = offset_y + (rect.max_.y - rect.min_.y);
    rect.min_.y = offset_y;
  
    if (rect.max_.y > texture.max_.y)
    {
      std::cout << "rect max y = " << rect.max_.y << ", tex max y = " << texture.max_.y << std::endl;
      std::cout << "Rect " << i << "(" << rect.max_.x - rect.min_.x << " x " << rect.max_.y - rect.min_.y << ") doesn't fit on texture\n";

      //recursive call until all rectangles fir on texture
      std::cout << "Repacking....(" << scale_factor*1.1 << ")\n";
      rectangle rtn_tex_rect = pack(starting_rects, scale_factor * 1.1);
      input = starting_rects;

      return rtn_tex_rect;

    }

  }

  //done

#if 0

  //print the result
  //for (int i=0; i< input.size(); i++){
  //  auto& rect = input[i];
  //  std::cout<< "rectangle["<< rect.id_<< "]"<<"  min("<< rect.min_.x<<" ,"<< rect.min_.y<<")"<<std::endl;
  //  std::cout<< "rectangle["<< rect.id_<< "]"<<"  max("<< rect.max_.x<< " ,"<<rect.max_.y<<")"<<std::endl;
  //}

  //output test image for rectangle packing
  std::vector<unsigned char> image;
  image.resize(texture.max_.x*texture.max_.y*4);
  for (int i = 0; i < image.size()/4; ++i) {
    image[i*4+0] = 255;
    image[i*4+1] = 0;
    image[i*4+2] = 0;
    image[i*4+3] = 255;
  }
  for (int i=0; i< input.size(); i++){
    auto& rect = input[i];
    int color = (255/input.size())*rect.id_;
    for (int x = rect.min_.x; x < rect.max_.x; x++) {
      for (int y = rect.min_.y; y < rect.max_.y; ++y) {
        int pixel = (x + texture.max_.x*y) * 4;
           image[pixel] = (char)color;
           image[pixel+1] = (char)color;
           image[pixel+2] = (char)color;
           image[pixel+3] = (char)255;
      }
    }
  }
  save_image("data/mesh_prepro_texture.png", image, texture.max_.x, texture.max_.y);

#endif

  return texture;
}


// void write_projection_info_file(std::vector<chart>& charts, std::string outfile_name){

//   std::ofstream ofs( outfile_name, std::ios::binary);

//   //write number of charts
//   uint32_t num_charts = charts.size();
//   ofs.write((char*) &num_charts, sizeof(num_charts));


//   for (auto& chart : charts)
//   {
//     ofs.write((char*) &(chart.projection), sizeof(projection_info));
//   }

//   ofs.close();

//   std::cout << "Wrote projection file to " << outfile_name << std::endl;
// }


//subroutine for error-checking during shader compilation
GLint compile_shader(const std::string& _src, GLint _shader_type) {

  const char* shader_src = _src.c_str();
  GLuint shader = glCreateShader(_shader_type);
  
  glShaderSource(shader, 1, &shader_src, NULL);
  glCompileShader(shader);

  GLint status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    GLint log_length;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);

    GLchar* log = new GLchar[log_length + 1];
    glGetShaderInfoLog(shader, log_length, NULL, log);

    const char* type = NULL;
    switch (_shader_type) {
      case GL_VERTEX_SHADER: type = "vertex"; break;
      case GL_FRAGMENT_SHADER: type = "fragment"; break;
      default: break;
    }

    std::string error_message = "compile shader failure in " + std::string(type) + " shader:\n" + std::string(log);
    delete[] log;
    std::cout << error_message << std::endl;
    exit(1);
  }

  return shader;

}

//compile and link the shader programs
void make_shader_program() {

  //locates vertices at new uv position on screen
  //passes old uvs in order to read from the texture 
  
  std::string vertex_shader_src = "#version 420\n\
    layout (location = 0) in vec2 vertex_old_coord;\n\
    layout (location = 1) in vec2 vertex_new_coord;\n\
    \n\
    uniform vec2 viewport_offset;\n\
    uniform vec2 viewport_scale;\n\
    \n\
    varying vec2 passed_uv;\n\
    \n\
    void main() {\n\
      vec2 coord = vec2(vertex_new_coord.x, vertex_new_coord.y);\n\
      vec2 coord_translated = coord - viewport_offset; \n\
      vec2 coord_scaled = coord_translated / viewport_scale; \n\
      gl_Position = vec4((coord_scaled-0.5)*2.0, 0.5, 1.0);\n\
      passed_uv = vertex_old_coord;\n\
    }";

  std::string fragment_shader_src = "#version 420\n\
    uniform sampler2D image;\n\
    varying vec2 passed_uv;\n\
    \n\
    layout (location = 0) out vec4 fragment_color;\n\
    \n\
    void main() {\n\
      vec4 color = texture(image, passed_uv).rgba;\n\
      fragment_color = vec4(color.rgb, 1.0);\n\
    }";


  //compile shaders
  GLint vertex_shader = compile_shader(vertex_shader_src, GL_VERTEX_SHADER);
  GLint fragment_shader = compile_shader(fragment_shader_src, GL_FRAGMENT_SHADER);

  //create the GL resource and save the handle for the shader program
  shader_program_ = glCreateProgram();
  glAttachShader(shader_program_, vertex_shader);
  glAttachShader(shader_program_, fragment_shader);
  glLinkProgram(shader_program_);

  //since the program is already linked, we do not need to keep the separate shader stages
  glDetachShader(shader_program_, vertex_shader);
  glDeleteShader(vertex_shader);
  glDetachShader(shader_program_, fragment_shader);
  glDeleteShader(fragment_shader);
}

//on every 16 ms
static void glut_timer(int32_t _e) {
  glutPostRedisplay();
  glutTimerFunc(16, glut_timer, 1);
  elapsed_ms_ += 16;
}



void glut_display() {

  //set the viewport size
  glViewport(0, 0, (GLsizei)window_width_, (GLsizei)window_height_);

  //set background colour
  glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
  // glClearColor(0.5f, 0.5f, 0.5f, 0.0f);

  //create a vertex buffer 
  glGenBuffers(1, &vertex_buffer_);

  //for each viewport
  for (uint32_t view_id = 0; view_id < viewports.size(); ++view_id)
  {
    std::cout << "Rendering into viewport " << view_id << std::endl;

    viewport vport = viewports[view_id];

    std::cout << "Viewport start: " << vport.normed_offset.x << ", " << vport.normed_offset.y << std::endl;
    std::cout << "Viewport size: " << vport.normed_dims.x << ", " << vport.normed_dims.y << std::endl;

    frame_buffer_->enable();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // for each texture
    for (uint32_t i = 0; i < to_upload_per_texture.size(); ++i)
    {
      
      num_vertices_ = to_upload_per_texture[i].size();

      if (num_vertices_ == 0)
      {
        std::cout << "Nothing to render for texture (id) " << i << " (path) "<< texture_paths[i] << std::endl;
        continue;
      }

      std::cout << "Rendering from texture (id) " << i << " (path) "<< texture_paths[i] << std::endl;



      //convert output texture coordinates
      std::vector<blit_vertex> updated_vertices;

      uint32_t vertex_count = 0;
      for (auto v : to_upload_per_texture[i])
      {
        blit_vertex new_bv = v;
        //do conversion into new viewport space for every vertex
        //shift tex coords
        new_bv.new_coord_ = scm::math::vec2f(v.new_coord_.x/* - vport.normed_offset.x*/, v.new_coord_.y /* - vport.normed_offset.y*/);
        //scale tex coords
        new_bv.new_coord_ = scm::math::vec2f(v.new_coord_.x/* / vport.normed_dims.x*/, v.new_coord_.y/* / vport.normed_dims.y */);

        updated_vertices.push_back(new_bv);
      }

      std::cout << "TO UPLOAD PER VERTEX SIZE: " << to_upload_per_texture[i].size() << std::endl;


      glUseProgram(shader_program_);


      //upload this vector to GPU (vertex_buffer_)
      glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
      glBufferData(GL_ARRAY_BUFFER, num_vertices_*sizeof(blit_vertex), &updated_vertices[0], GL_STREAM_DRAW);

      //bind the VBO of the model such that the next draw call will render with these vertices
      glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);

      //define the layout of the vertex buffer:
      //setup 2 attributes per vertex (2x texture coord)
      glEnableVertexAttribArray(0);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(blit_vertex), (void*)0);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(blit_vertex), (void*)(2*sizeof(float)));

      //get texture location
      int slot = 0;
      glUniform1i(glGetUniformLocation(shader_program_, "image"), slot);
      glUniform2f(glGetUniformLocation(shader_program_, "viewport_offset"), vport.normed_offset[0], vport.normed_offset[1]);
      glUniform2f(glGetUniformLocation(shader_program_, "viewport_scale"), vport.normed_dims[0], vport.normed_dims[1]);

      glActiveTexture(GL_TEXTURE0 + slot);
      
      //here, enable the current texture
      // texture_->enable(slot);
      textures_[i]->enable(slot);

      //draw triangles from the currently bound buffer
      glDrawArrays(GL_TRIANGLES, 0, num_vertices_);

      //unbind, unuse
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glUseProgram(0);

      textures_[i]->disable();


    }//end for each texture

    frame_buffer_->disable();

    save_image(outfile_name.substr(0,outfile_name.length()-4) + std::to_string(view_id) + ".png", frame_buffer_);

  } //end for each viewport


  exit(1);

  glutSwapBuffers();
}


double get_area_of_triangle(scm::math::vec2f v0, scm::math::vec2f v1, scm::math::vec2f v2){

  return 0.5 * scm::math::length(scm::math::cross(scm::math::vec3f(v1-v0,0.0), scm::math::vec3f(v2-v0,0.0)));
}

//calculates average pixels per triangle for each chart, given a texture size
// use first argument to determine whether measurement should use old or new tex coords
enum PixelResolutionCalculationType { USE_OLD_COORDS, USE_NEW_COORDS};
void calculate_chart_tex_space_sizes(PixelResolutionCalculationType type, 
                                     std::vector<chart>& charts, 
                                     std::vector<triangle>& triangles, 
                                     std::vector<scm::math::vec2i> texture_dimensions){

  //calculate pixel area for each dimension recieved
  std::vector<double> pixel_areas;
  for (auto& tex_size : texture_dimensions){
    pixel_areas.push_back( (1.0 / tex_size.x) * (1.0 / tex_size.y) );
  }

  if (type == USE_OLD_COORDS){
    std::cout << "Calculating pixel sizes with old coords...";
  }
  else {
    std::cout << "Calculating pixel sizes with new coords...";
  }

  //for all charts
  for(auto& chart : charts){

    double pixels = 0.0;

    //for all triangles
    for (auto& tri_id : chart.all_triangle_ids_)
    {


      //sample length  between vertices 0 and 1
      auto& tri = triangles[tri_id];

      int texture_id = tri.tex_id;

      //check this texture is error free
      if (texture_id == -1){continue;}


      //calculate areas in texture space
      double tri_area, tri_pixels;
      if (type == USE_OLD_COORDS)
      {
        tri_area = get_area_of_triangle(tri.v0_.old_coord_, tri.v1_.old_coord_, tri.v2_.old_coord_);
        //if we are calculating size of tris on input textures, we should use the appropiate pixel area 
        tri_pixels = tri_area / pixel_areas[texture_id];
      }
      else {
        tri_area = get_area_of_triangle(chart.all_triangle_new_coods_[tri_id][0], chart.all_triangle_new_coods_[tri_id][1], chart.all_triangle_new_coods_[tri_id][2]);
        //if we are calculating size of tris on output textures,pixel area is constant 
        tri_pixels = tri_area / pixel_areas[0];
      }

      pixels += tri_pixels;
    }

    double pixels_per_tri = pixels / chart.all_triangle_ids_.size();

    if (type == USE_OLD_COORDS)
    {
      chart.real_to_tex_ratio_old = pixels_per_tri;
    }
    else {
      chart.real_to_tex_ratio_new = pixels_per_tri;
    }
  }

  std::cout << "done\n";

}

//checks if at least a given percentage of charts have at least as many pixels now as they did in the original texture
bool is_output_texture_big_enough(std::vector<chart>& charts, double target_percentage_charts_with_enough_pixels){
  int charts_w_enough_pixels = 0;

  for (auto& chart : charts)
  {
    if (chart.real_to_tex_ratio_new >= chart.real_to_tex_ratio_old){
      charts_w_enough_pixels++;
    }
  }

  const double percentage_big_enough = (double)charts_w_enough_pixels / charts.size();

  std::cout << "Percentage of charts big enough  = " << percentage_big_enough << std::endl;

  return (percentage_big_enough >= target_percentage_charts_with_enough_pixels);
}


int main(int argc, char *argv[]) {
    

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(window_width_, window_height_);
    glutInitWindowPosition(64, 64);
    glutCreateWindow(argv[0]);
    glutSetWindowTitle("texture renderer");
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glewExperimental = GL_TRUE;
    glewInit();


    if (argc == 1 || 
      cmd_option_exists(argv, argv+argc, "-h") ||
      !cmd_option_exists(argv, argv+argc, "-f")) {
      
      std::cout << "Usage: " << argv[0] << "<flags> -f <input_file>" << std::endl <<
         "INFO: bvh_leaf_extractor " << std::endl <<
         "\t-f: selects .bvh input file" << std::endl <<
         "\t-c: selects .lodchart input file (default = <bvhname>.lodchart)" << std::endl <<
         "\t-t: selects .png texture input file (default = <bvhname>.png)" << std::endl <<
         "\t-single-max: specifies largest possible single output texture (=8192)" << std::endl <<
         "\t-multi-max: specifies largest possible output texture (=total size of 4 x single_tex_limit (=32768))" << std::endl <<
         std::endl;
      return 0;
    }

    std::string bvh_filename = std::string(get_cmd_option(argv, argv + argc, "-f"));
    std::string lod_filename = bvh_filename.substr(0, bvh_filename.size()-4) + ".lod";

    std::string chart_lod_filename = bvh_filename.substr(0,bvh_filename.length()-4).append(".lodchart");
    if (cmd_option_exists(argv, argv+argc, "-c")) {
      chart_lod_filename = get_cmd_option(argv, argv+argc, "-c");
    }

    std::string texture_filename = bvh_filename.substr(0,bvh_filename.length()-4).append(".png");
    if (cmd_option_exists(argv, argv+argc, "-t")) {
      texture_filename = get_cmd_option(argv, argv+argc, "-t");
    }

    int single_tex_limit = 8192;
    if (cmd_option_exists(argv, argv+argc, "-single-max")) {
      single_tex_limit = atoi(get_cmd_option(argv, argv+argc, "-single-max"));
      std::cout << "Single output texture limited to " << single_tex_limit << std::endl;
    }

    window_width_ = single_tex_limit;
    window_height_ = single_tex_limit;

    int multi_tex_limit = single_tex_limit * 4;
    if (cmd_option_exists(argv, argv+argc, "-multi-max")) {
      multi_tex_limit = atoi(get_cmd_option(argv, argv+argc, "-multi-max"));
      std::cout << "Multi output texture limited to " << multi_tex_limit << std::endl;
    }
    //default (minimum) output texture size = single texture size
    window_width_ = single_tex_limit;
    window_height_ = single_tex_limit;
    std::cout << "Single texture size limit: " << single_tex_limit << std::endl;
    std::cout << "Multi texture size limit: " << multi_tex_limit << std::endl;
    



    std::vector<int> chart_id_per_triangle;
    int num_charts = load_chart_file(chart_lod_filename, chart_id_per_triangle);
    std::cout << "lodchart file loaded." << std::endl;

    std::shared_ptr<lamure::ren::bvh> bvh = std::make_shared<lamure::ren::bvh>(bvh_filename);
    std::cout << "bvh file loaded." << std::endl;

    std::shared_ptr<lamure::ren::lod_stream> lod = std::make_shared<lamure::ren::lod_stream>();
    lod->open(lod_filename);
    std::cout << "lod file loaded." << std::endl;


    std::vector<chart> charts;
    //initially, lets compile the charts

    for (int chart_id = 0; chart_id < num_charts; ++chart_id) {
      rectangle rect{
        scm::math::vec2f(std::numeric_limits<float>::max()),
        scm::math::vec2f(std::numeric_limits<float>::lowest()),
        chart_id, 
        false};
      lamure::bounding_box box(
        scm::math::vec3d(std::numeric_limits<float>::max()),
        scm::math::vec3d(std::numeric_limits<float>::lowest()));
      
      charts.push_back(chart{chart_id, rect, box, std::set<int>()});
    }


    //compute the bounding box of all triangles in a chart
    size_t vertices_per_node = bvh->get_primitives_per_node();
    size_t size_of_vertex = bvh->get_size_of_primitive();
    
    std::vector<lamure::ren::dataset::serialized_vertex> vertices;
    vertices.resize(vertices_per_node);

    std::cout << "lod file read with " << bvh->get_num_nodes() << " nodes\n";
    std::cout << vertices_per_node <<  " vertices per node\n";
    std::cout << "vertex size = " << size_of_vertex << std::endl;


    //for each node in lod
    for (uint32_t node_id = 0; node_id < bvh->get_num_nodes(); node_id++) {
      
      lod->read((char*)&vertices[0], (vertices_per_node*node_id*size_of_vertex),
        (vertices_per_node * size_of_vertex));

      //for every vertex
      for (int vertex_id = 0; vertex_id < vertices_per_node; ++vertex_id) {

        int chart_id = chart_id_per_triangle[node_id*(vertices_per_node/3)+vertex_id/3];
        auto& chart = charts[chart_id];

        const auto& vertex = vertices[vertex_id];
        scm::math::vec3d v(vertex.v_x_, vertex.v_y_, vertex.v_z_);

        scm::math::vec3d min_vertex = chart.box_.min();
        scm::math::vec3d max_vertex = chart.box_.max();


        min_vertex.x = std::min(min_vertex.x, v.x);
        min_vertex.y = std::min(min_vertex.y, v.y);
        min_vertex.z = std::min(min_vertex.z, v.z);

        max_vertex.x = std::max(max_vertex.x, v.x);
        max_vertex.y = std::max(max_vertex.y, v.y);
        max_vertex.z = std::max(max_vertex.z, v.z);

        chart.box_ = lamure::bounding_box(min_vertex, max_vertex);

      }
    }

    //compare all triangles with chart bounding boxes
    uint32_t first_leaf = bvh->get_first_node_id_of_depth(bvh->get_depth());
    uint32_t num_leafs = bvh->get_length_of_depth(bvh->get_depth());

    for (int chart_id = 0; chart_id < num_charts; ++chart_id) {

      //intersect all bvh leaf nodes with chart bounding boxes
      auto& chart = charts[chart_id];

      for (int leaf_id = first_leaf; leaf_id < first_leaf+num_leafs; ++leaf_id) {

#if 1
        lamure::bounding_box leaf_box(
          scm::math::vec3d(bvh->get_bounding_boxes()[leaf_id].min_vertex()),
          scm::math::vec3d(bvh->get_bounding_boxes()[leaf_id].max_vertex()));

        //disabled - adding any triangles that intersect chart projection
        // //broad check that the leaf and chart intersect before checking individual triangles
         if (chart.box_.intersects(leaf_box)) {

           lod->read((char*)&vertices[0], (vertices_per_node*leaf_id*size_of_vertex),
             (vertices_per_node * size_of_vertex));

           for (int vertex_id = 0; vertex_id < vertices_per_node; ++vertex_id) {
             const auto& vertex = vertices[vertex_id];
             scm::math::vec3d v(vertex.v_x_, vertex.v_y_, vertex.v_z_);

             //find out if this triangle intersects the chart box
             if (chart.box_.contains(v)) {
               int lod_tri_id = (leaf_id-first_leaf)*(vertices_per_node/3)+(vertex_id/3);
               chart.all_triangle_ids_.insert(lod_tri_id);

               //is it ok to add the same triangle multiple times?
             }
           }
         }

         for (int tri_id = 0; tri_id < vertices_per_node/3; ++tri_id) {
          
          if (chart_id == chart_id_per_triangle[leaf_id*(vertices_per_node/3)+tri_id]) {
            int lod_tri_id = (leaf_id-first_leaf)*(vertices_per_node/3)+tri_id;
            chart.original_triangle_ids_.insert(lod_tri_id);
          }
        }

#else
        //get tri id and then chart id from lodchart list
        //if chart is this chart, add to appropriate tri list
        for (int tri_id = 0; tri_id < vertices_per_node/3; ++tri_id) {
          
          if (chart_id == chart_id_per_triangle[leaf_id*(vertices_per_node/3)+tri_id]) {
            int lod_tri_id = (leaf_id-first_leaf)*(vertices_per_node/3)+tri_id;
            chart.all_triangle_ids_.insert(lod_tri_id);
            chart.original_triangle_ids_.insert(lod_tri_id);
          }
        }
#endif
    }
  }



    //report chart parameters
    for (int chart_id = 0; chart_id < num_charts; ++chart_id) {
      auto& chart = charts[chart_id];
      std::cout << "chart id " << chart_id << std::endl;
      std::cout << "box min " << chart.box_.min() << std::endl;
      std::cout << "box max " << chart.box_.max() << std::endl;
      std::cout << "original triangles " << chart.original_triangle_ids_.size() << std::endl;
      std::cout << "all triangles " << chart.all_triangle_ids_.size() << std::endl;
    }

    std::cout << "tris per node " << vertices_per_node/3 << std::endl;


    //for each leaf triangle in lod, build a triangle struct from serialised vertices
    std::vector<triangle> triangles;
    for (uint32_t leaf_id = first_leaf; leaf_id < first_leaf+num_leafs; ++leaf_id) {

      lod->read((char*)&vertices[0], (vertices_per_node*leaf_id*size_of_vertex),
        (vertices_per_node * size_of_vertex));

      for (int tri_id = 0; tri_id < vertices_per_node/3; ++tri_id) {

        uint32_t triangle_offset = tri_id*3;

        triangle tri;

        auto& vertex_0 = vertices[triangle_offset];
        tri.v0_.pos_ = scm::math::vec3f(vertex_0.v_x_, vertex_0.v_y_, vertex_0.v_z_);
        tri.v0_.nml_ = scm::math::vec3f(vertex_0.n_x_, vertex_0.n_y_, vertex_0.n_z_);
        tri.v0_.old_coord_ = scm::math::vec2f(vertex_0.c_x_, vertex_0.c_y_);

        auto& vertex_1 = vertices[triangle_offset + 1];
        tri.v1_.pos_ = scm::math::vec3f(vertex_1.v_x_, vertex_1.v_y_, vertex_1.v_z_);
        tri.v1_.nml_ = scm::math::vec3f(vertex_1.n_x_, vertex_1.n_y_, vertex_1.n_z_);
        tri.v1_.old_coord_ = scm::math::vec2f(vertex_1.c_x_, vertex_1.c_y_);

        auto& vertex_2 = vertices[triangle_offset + 2];
        tri.v2_.pos_ = scm::math::vec3f(vertex_2.v_x_, vertex_2.v_y_, vertex_2.v_z_);
        tri.v2_.nml_ = scm::math::vec3f(vertex_2.n_x_, vertex_2.n_y_, vertex_2.n_z_);
        tri.v2_.old_coord_ = scm::math::vec2f(vertex_2.c_x_, vertex_2.c_y_);

        triangles.push_back(tri);
      }
    }

    std::cout << "Created list of " << triangles.size() << " leaf level triangles\n";


    //load texture ids and apply to triangles
    std::string tex_id_file_name = bvh_filename.substr(0,bvh_filename.length()-4).append(".lodtexid");
    int first_leaf_triangle_id = first_leaf * (vertices_per_node / 3);
    int num_textures = load_tex_ids(tex_id_file_name, triangles, first_leaf_triangle_id);
    std::vector<scm::math::vec2i> texture_dimensions;

    // if multiple input textures were found
    if (num_textures > 0)
    {
      //load mtl to get texture paths
      std::cout << "loading mtl..." << std::endl;

      std::string mtl_filename = bvh_filename.substr(0, bvh_filename.size()-11) + ".mtl"; //remove "_charts.bvh" suffix from bvh name 
      std::cout << "MTL-FILENAME: " << mtl_filename << std::endl;


      std::vector<int> missing_textures = Utils::load_tex_paths_from_mtl(mtl_filename, texture_paths, texture_dimensions);

      if (texture_paths.size() < num_textures)
      {
        std::cout << "WARNING: not enough textures were found in the material file\n";
        num_textures = texture_paths.size();
      }

      //Replace texture ids of textures that are missing with -1 in triangle list
      if (missing_textures.size() > 0)
      {
        for (auto& tri : triangles)
        {
          for (int i = 0; i < missing_textures.size(); ++i)
          {
            if (tri.tex_id == missing_textures[i])
            {
              tri.tex_id = -1;
              break;
            }
          }
        }
      }

    }
    //if only one texture was found
    else {
      num_textures = 1;
      //check existence as a png
      bool file_good = true;
      if(!boost::filesystem::exists(texture_filename)) {file_good = false;}
      if(!boost::algorithm::ends_with(texture_filename, ".png")) {file_good = false;}

      if (!file_good)
      {
        std::cout << "ERROR: Input texture file was not found (path: " << texture_filename << ")\n";
        return 0;
      }
      //if file is good, add info about path and size to texture info arrays
      texture_paths.push_back(texture_filename);
      texture_dimensions.push_back(Utils::get_png_dimensions(texture_filename));

    }

    //todo
    //use sizes when calculating texture space sizes in function below


    //for each chart, calculate relative size of real space to original tex space
    calculate_chart_tex_space_sizes(USE_OLD_COORDS, charts, triangles, texture_dimensions);

    project(charts, triangles);



    std::cout << "running rectangle packing" << std::endl;

    const float scale = 400.f;

    //scale the rectangles
    std::vector<rectangle> rects;
    for (int chart_id = 0; chart_id < charts.size(); ++chart_id) {
      chart& chart = charts[chart_id];
      rectangle rect = chart.rect_;
      rect.max_.x *= scale;
      rect.max_.y *= scale;
      rects.push_back(rect);

      std::cout << " Rect " << rect.max_.x << "x" << rect.max_.y << std::endl; 
    }

    //rectangle packing
    rectangle image = pack(rects);
    std::cout << "final image " << image.max_.x << " " << image.max_.y << std::endl;

    //apply rectangles
    int id = 0;
    for (const auto& rect : rects) {
      charts[rect.id_].rect_ = rect;

      std::cout << "rect id " << id << " " << rect.min_.x << " " << rect.min_.y << std::endl;
      std::cout << "rect id " << id << " " << rect.max_.x << " " << rect.max_.y << std::endl;

      charts[rect.id_].projection.tex_space_rect = rect;//save for rendering from texture later on
    }





  //apply new coordinates and pack tris

  //pack tris

  //use 2D array to account for different textures (if no textures were found, make sure it has at least one row)
  to_upload_per_texture.resize(std::max(num_textures,1));

  

  for (int chart_id = 0; chart_id < charts.size(); ++chart_id) {
    chart& chart = charts[chart_id];
    rectangle& rect = chart.rect_;

    for (auto tri_id : chart.all_triangle_ids_) {


       if (rect.flipped_) {
         float temp = chart.all_triangle_new_coods_[tri_id][0].x;
         chart.all_triangle_new_coods_[tri_id][0].x = chart.all_triangle_new_coods_[tri_id][0].y;
         chart.all_triangle_new_coods_[tri_id][0].y = temp;

         temp = chart.all_triangle_new_coods_[tri_id][1].x;
         chart.all_triangle_new_coods_[tri_id][1].x = chart.all_triangle_new_coods_[tri_id][1].y;
         chart.all_triangle_new_coods_[tri_id][1].y = temp;

         temp = chart.all_triangle_new_coods_[tri_id][2].x;
         chart.all_triangle_new_coods_[tri_id][2].x = chart.all_triangle_new_coods_[tri_id][2].y;
         chart.all_triangle_new_coods_[tri_id][2].y = temp;

         
       }


       for(int vert_idx = 0; vert_idx < 3; ++vert_idx) {
         chart.all_triangle_new_coods_[tri_id][vert_idx] *= scale;
         chart.all_triangle_new_coods_[tri_id][vert_idx].x += rect.min_.x;
         chart.all_triangle_new_coods_[tri_id][vert_idx].y += rect.min_.y;
         chart.all_triangle_new_coods_[tri_id][vert_idx].x /= image.max_.x;
         chart.all_triangle_new_coods_[tri_id][vert_idx].y /= image.max_.x;
       }


      //pack to 2D upload array
      //limit texture id to number of textures that were found
      int texture_id = std::min( triangles[tri_id].tex_id , num_textures );
      if (texture_id != -1)
      {
        to_upload_per_texture[texture_id].push_back(blit_vertex{triangles[tri_id].v0_.old_coord_, chart.all_triangle_new_coods_[tri_id][0]});
        to_upload_per_texture[texture_id].push_back(blit_vertex{triangles[tri_id].v1_.old_coord_, chart.all_triangle_new_coods_[tri_id][1]});
        to_upload_per_texture[texture_id].push_back(blit_vertex{triangles[tri_id].v2_.old_coord_, chart.all_triangle_new_coods_[tri_id][2]});
      } else {
        std::cout << "TEXID INVALID" << std::endl;

        std::cout << "TEXID: texid: " <<  triangles[tri_id].tex_id << " NUM TEXTURES: " << num_textures << "\n";
      }

    } 
  }

    
  //for each chart, calculate relative size of real space to new tex space
  calculate_chart_tex_space_sizes(USE_NEW_COORDS, charts, triangles, std::vector<scm::math::vec2i>{scm::math::vec2i(window_width_, window_height_)});

  //   //print pixel ratios per chart
  // for (auto& chart : charts)
  // {
  //   std::cout << "Chart " << chart.id_ << ": old ratio " << chart.real_to_tex_ratio_old << std::endl;
  //   std::cout << "-------- " << ": new ratio " << chart.real_to_tex_ratio_new << std::endl;
  // }


  //double texture size up to 8k if a given percentage of charts do not have enough pixels
  const double target_percentage_charts_with_enough_pixels = 0.99;
  std::cout << "Testing texture size of " << window_width_ << " x " << window_height_ << std::endl;
  while (!is_output_texture_big_enough(charts, target_percentage_charts_with_enough_pixels)) {

    //limit texture size
    

    if (std::max(window_width_, window_height_) >= multi_tex_limit){
      std::cout << "Max texture size reached\n";
      break;
    }

    window_width_  *= 2;
    window_height_ *= 2;

    std::cout << "Not enough pixels! Texture increased to " << window_width_ << " x " << window_height_ << std::endl;

    calculate_chart_tex_space_sizes(USE_NEW_COORDS, charts, triangles, std::vector<scm::math::vec2i>{scm::math::vec2i(window_width_, window_height_)});

  }


  //if output texture is bigger than 8k, create a set if viewports that will be rendered separately
  if (window_width_ > single_tex_limit || window_height_ > single_tex_limit)
  {
    //calc num of viewports needed from size of output texture
    int viewports_w, viewports_h;
    viewports_w = std::ceil(window_width_ / single_tex_limit);
    viewports_h = std::ceil(window_height_ / single_tex_limit);

    scm::math::vec2f viewport_normed_size (1.f / viewports_w, 1.f / viewports_h);

    //create a vector of viewports needed 
    for (int y = 0; y < viewports_h; ++y)
    {
      for (int x = 0; x < viewports_w; ++x)
      {
        viewport vp;
        vp.normed_dims = viewport_normed_size;
        vp.normed_offset = scm::math::vec2f(viewport_normed_size.x * x, viewport_normed_size.y * y);
        viewports.push_back(vp);

        std::cout << "Viewport " << viewports.size() -1 << ": " << viewport_normed_size.x << "x" << viewport_normed_size.y << " at (" << vp.normed_offset.x << "," << vp.normed_offset.y << ")\n";
      }
    }

    //set window size as size of 1 viewport
    window_width_ = single_tex_limit;
    window_height_ = single_tex_limit;

  }
  else {
    viewport single_viewport;
    single_viewport.normed_offset = scm::math::vec2f(0.f,0.f);
    single_viewport.normed_dims = scm::math::vec2f(1.0,1.0);
    viewports.push_back(single_viewport);

    std::cout << "Viewport " << viewports.size() -1 << ": " << single_viewport.normed_dims.x << "x" << single_viewport.normed_dims.y << " at (" << single_viewport.normed_offset.x << "," << single_viewport.normed_offset.y << ")\n";

  }
  std::cout << "Created " << viewports.size() << " viewports to render multiple output textures" << std::endl;


    //replacing texture coordinates in LOD file
  std::cout << "replacing tex coords for inner nodes..." << std::endl;
  std::string out_lod_filename = bvh_filename.substr(0, bvh_filename.size()-4) + "_uv.lod";
  std::shared_ptr<lamure::ren::lod_stream> lod_out = std::make_shared<lamure::ren::lod_stream>();
  lod_out->open_for_writing(out_lod_filename);
 
  for (uint32_t node_id = 0; node_id < bvh->get_num_nodes(); ++node_id) { //loops only inner nodes

    //load the node
    lod->read((char*)&vertices[0], (vertices_per_node*node_id*size_of_vertex),
      (vertices_per_node * size_of_vertex));

    //if leaf node, just replace tex coord from corresponding triangle
    if (node_id >= first_leaf && node_id < (first_leaf+num_leafs) ) {


      for (int vertex_id = 0; vertex_id < vertices_per_node; ++vertex_id) {
        int tri_id = ((node_id - first_leaf)*(vertices_per_node/3))+(vertex_id/3);

        int lod_tri_id = (node_id)*(vertices_per_node/3)+(vertex_id/3);
        int chart_id = chart_id_per_triangle[lod_tri_id];

        if (chart_id != -1){

          switch (vertex_id % 3) {
            case 0:
            {
              vertices[vertex_id].c_x_ = charts[chart_id].all_triangle_new_coods_[tri_id][0].x;
              vertices[vertex_id].c_y_ = 1.0 - charts[chart_id].all_triangle_new_coods_[tri_id][0].y;
              break;
            }
            case 1:
            {
              vertices[vertex_id].c_x_ = charts[chart_id].all_triangle_new_coods_[tri_id][1].x;
              vertices[vertex_id].c_y_ = 1.0 -  charts[chart_id].all_triangle_new_coods_[tri_id][1].y;
              break;
            }
            case 2:
            {
              vertices[vertex_id].c_x_ = charts[chart_id].all_triangle_new_coods_[tri_id][2].x;
              vertices[vertex_id].c_y_ = 1.0 - charts[chart_id].all_triangle_new_coods_[tri_id][2].y;
              break;
            }
            default:
            break;
          }
          
        }

      }
    }

    // if inner node, project vertices onto plane to get tex coords 
    else
    {
      //for each vertex
      for (int vertex_id = 0; vertex_id < vertices_per_node; ++vertex_id) {

        auto& vertex = vertices[vertex_id];
        const int lod_tri_id = (node_id)*(vertices_per_node/3)+(vertex_id/3);
        const int chart_id = chart_id_per_triangle[lod_tri_id];
        //access projection info for the relevant chart

        if (chart_id != -1){

          auto& proj_info = charts[chart_id].projection;
          rectangle& rect = charts[chart_id].rect_;

           //at this point we will need to project all triangles of inner nodes to their respective charts using the corresponding chart plane
          scm::math::vec3f original_v (vertex.v_x_, vertex.v_y_, vertex.v_z_);
          scm::math::vec2f projected_v = project_to_plane(original_v, proj_info.proj_normal, proj_info.proj_centroid, proj_info.proj_world_up);
          
          //correct by offset (so that min uv coord = 0) 
          projected_v -= proj_info.tex_coord_offset;
          //apply normalisation factor
          projected_v /= proj_info.largest_max;
          //flip if needed
          if (rect.flipped_)
          {
            float temp = projected_v.x;
            projected_v.x = projected_v.y;
            projected_v.y = temp;
          }
          //scale
          projected_v *= scale;
          //offset position in texture
          projected_v += rect.min_;
          //scale down to normalised image space
          projected_v /= image.max_;
          //flip y coord
          projected_v.y = 1.0 - projected_v.y; 

          //replace existing coords in vertex array
          vertex.c_x_ = projected_v.x;
          vertex.c_y_ = projected_v.y;  

        }
      }
    }
    //afterwards, write the node to new file
    lod_out->write((char*)&vertices[0], (vertices_per_node*node_id*size_of_vertex),
      (vertices_per_node * size_of_vertex));
  }

  lod_out->close();
  lod_out.reset();
  lod->close();
  lod.reset();

  std::cout << "OUTPUT updated lod file: " << out_lod_filename << std::endl;

  //write output bvh
  std::string out_bvh_filename = bvh_filename.substr(0, bvh_filename.size()-4) + "_uv.bvh";
  bvh->write_bvh_file(out_bvh_filename);
  bvh.reset();
  std::cout << "OUTPUT updated bvh file: " << out_bvh_filename << std::endl;


  //save name for new texture
  outfile_name = bvh_filename.substr(0, bvh_filename.size()-4) + "_uv.png";

  //create output frame buffer
  frame_buffer_ = std::make_shared<frame_buffer_t>(1, window_width_, window_height_, GL_RGBA, GL_LINEAR);

  load_textures();

  //create shaders
  make_shader_program();

  //register glut-callbacks
  //the arguments are pointers to the functions we defined above
  glutDisplayFunc(glut_display);
  
  glutShowWindow();

  //set a timeout of 16 ms until glut_timer is triggered
  glutTimerFunc(16, glut_timer, 1);

  //start the main loop (which mainly calls the glutDisplayFunc)
  glutMainLoop();

    return 0;
}



