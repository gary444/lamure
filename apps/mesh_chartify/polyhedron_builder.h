#include "CGAL_typedefs.h"

#include<CGAL/Polyhedron_incremental_builder_3.h>

#include "kdtree.h"

template<class HDS>
class polyhedron_builder : public CGAL::Modifier_base<HDS> {

public:
  std::vector<double> vertices;
  std::vector<int>    tris;
  std::vector<double> tcoords;
  std::vector<int>    tindices;

  bool CHECK_VERTICES;

  std::vector<int> face_textures;

  polyhedron_builder( std::vector<double> &_vertices,
                      std::vector<int> &_tris,
                      std::vector<double> &_tcoords,
                      std::vector<int> &_tindices,
                      bool _CHECK_VERTICES,
                      std::vector<int> &_face_textures) 
                      : vertices(_vertices), 
                        tris(_tris), 
                        tcoords(_tcoords), 
                        tindices(_tindices), 
                        CHECK_VERTICES(_CHECK_VERTICES), 
                        face_textures(_face_textures)  {}



  polyhedron_builder( std::vector<indexed_triangle_t> &_triangles) 
                      : CHECK_VERTICES(true) {

     for (uint32_t tri_idx = 0; tri_idx < _triangles.size(); ++tri_idx) {
       vertices.push_back(_triangles[tri_idx].v0_.pos_.x);
       vertices.push_back(_triangles[tri_idx].v0_.pos_.y);
       vertices.push_back(_triangles[tri_idx].v0_.pos_.z);

       vertices.push_back(_triangles[tri_idx].v1_.pos_.x);
       vertices.push_back(_triangles[tri_idx].v1_.pos_.y);
       vertices.push_back(_triangles[tri_idx].v1_.pos_.z);

       vertices.push_back(_triangles[tri_idx].v2_.pos_.x);
       vertices.push_back(_triangles[tri_idx].v2_.pos_.y);
       vertices.push_back(_triangles[tri_idx].v2_.pos_.z);

       tris.push_back(tri_idx*3);
       tris.push_back(tri_idx*3+1);
       tris.push_back(tri_idx*3+2);

       tcoords.push_back(_triangles[tri_idx].v0_.tex_.x);
       tcoords.push_back(_triangles[tri_idx].v0_.tex_.y);

       tcoords.push_back(_triangles[tri_idx].v1_.tex_.x);
       tcoords.push_back(_triangles[tri_idx].v1_.tex_.y);

       tcoords.push_back(_triangles[tri_idx].v2_.tex_.x);
       tcoords.push_back(_triangles[tri_idx].v2_.tex_.y);

       tindices.push_back(tri_idx*3);
       tindices.push_back(tri_idx*3+1);
       tindices.push_back(tri_idx*3+2);

       face_textures.push_back(_triangles[tri_idx].tex_idx_);

     }

  }



  //Returns false for error
  void operator()( HDS& hds) {


    // create a cgal incremental builder
    CGAL::Polyhedron_incremental_builder_3<HDS> B( hds, true);
    B.begin_surface( vertices.size()/3, tris.size()/3 );


    uint32_t degenerate_faces = 0;

    //if there is different coordinates for every vertex, then we probably need to create an indexed vertex list
    //if (tris.size()*3 == vertices.size())
    if (true)
    {
      std::cout << "Creating polyhedron indexed vertex list...\n";

      //create indexed vertex list
      std::vector<XtndPoint<Kernel> > vertices_indexed;
      std::vector<uint32_t> tris_indexed;
      create_indexed_triangle_list(tris_indexed, vertices_indexed);

      //add vertices of surface
      //std::cout << "Polyhedron builder: adding vertices\n";
      for (uint32_t i = 0; i < vertices_indexed.size(); ++i) {
        Vertex_handle vh = B.add_vertex(vertices_indexed[i]);
        vh->id = i;
      }

      //create faces using vertex index references
      //std::cout << "Polyhedron builder: adding faces\n";
      uint32_t face_count = 0;
      for (uint32_t i = 0; i < tris_indexed.size(); i+=3) {

        //check for degenerate faces:
        bool degenerate = is_face_degenerate(
                    Point( vertices_indexed[tris_indexed[i]] ),
                    Point( vertices_indexed[tris_indexed[i+1]] ),
                    Point( vertices_indexed[tris_indexed[i+2]] )
                    );

        if (!degenerate){

          Facet_handle fh = B.begin_facet();
          fh->id() = face_count++;

                    //add tex coords 
          TexCoord tc1 ( vertices_indexed[tris_indexed[i]].get_u() , vertices_indexed[tris_indexed[i]].get_v() );
          TexCoord tc2 ( vertices_indexed[tris_indexed[i+1]].get_u() , vertices_indexed[tris_indexed[i+1]].get_v() );
          TexCoord tc3 ( vertices_indexed[tris_indexed[i+2]].get_u() , vertices_indexed[tris_indexed[i+2]].get_v() );
          fh->add_tex_coords(tc1,tc2,tc3);

          //Add texture id for face
          if (face_textures.size() > 0)
          {
            fh->tex_id = face_textures[i/3];
          }

          B.add_vertex_to_facet(tris_indexed[i]);
          B.add_vertex_to_facet(tris_indexed[i+1]);
          B.add_vertex_to_facet(tris_indexed[i+2]);
          B.end_facet();
        }
        else {
          degenerate_faces++;
        }



      }
    }
    else {


      std::cout << "Loaded obj already indexed - creating polyhedron directly\n";

      std::cout << "Polyhedron builder: adding vertices\n";
      for( int i=0; i<(int)vertices.size() / 3; ++i ){

        Vertex_handle vh = B.add_vertex( XtndPoint<Kernel>( 
                             vertices[(i*3)], 
                             vertices[(i*3)+1], 
                             vertices[(i*3)+2],
                              tcoords[i*2],
                              tcoords[(i*2)+1]));
        vh->id = i;

      }

      // add the polyhedron triangles
      std::cout << "Polyhedron builder: adding faces\n";
      uint32_t face_count = 0;
      for( int i=0; i<(int)(tris.size()); i+=3 ){ 


        //check for degenerate faces:
        bool degenerate = is_face_degenerate(
                    Point( vertices[(tris[i+0]) * 3], vertices[((tris[i+0]) * 3) + 1], vertices[((tris[i+0]) * 3) + 2] ),
                    Point( vertices[(tris[i+1]) * 3], vertices[((tris[i+1]) * 3) + 1], vertices[((tris[i+1]) * 3) + 2] ),
                    Point( vertices[(tris[i+2]) * 3], vertices[((tris[i+2]) * 3) + 1], vertices[((tris[i+2]) * 3) + 2] )
                    );

        if(!degenerate){

          Facet_handle fh = B.begin_facet();
          fh->id() = face_count++;

          //add tex coords 
          TexCoord tc1 ( tcoords[ tindices[i+0]*2 ] , tcoords[ (tindices[i+0]*2)+1 ] );
          TexCoord tc2 ( tcoords[ tindices[i+1]*2 ] , tcoords[ (tindices[i+1]*2)+1 ] );
          TexCoord tc3 ( tcoords[ tindices[i+2]*2 ] , tcoords[ (tindices[i+2]*2)+1 ] );
          fh->add_tex_coords(tc1,tc2,tc3);

          //Add texture id for face
          if (face_textures.size() > 0)
          {
            fh->tex_id = face_textures[i/3];
          }

          B.add_vertex_to_facet( tris[i+0] );
          B.add_vertex_to_facet( tris[i+1] );
          B.add_vertex_to_facet( tris[i+2] );
          B.end_facet();
        }
        else {
          degenerate_faces++;
        }

      }
    }


   

   
    // finish up the surface
    //std::cout << "Polyhedron builder: ending surface\n";
    //std::cout << "Discarded " << degenerate_faces << " degenerate faces\n";
    //std::cout << "---------------------------------------------\n";
    B.end_surface();

  }

  bool is_face_degenerate(Point v1, Point v2, Point v3){
    if (v1 == v2 || v1 == v3 || v2 == v3)
    {
      return true;
    }
    return false;
  }


    //creates indexed triangles list (indexes and vertices) from triangle list
  void create_indexed_triangle_list(std::vector<uint32_t>& tris_indexed,
                                    std::vector<XtndPoint<Kernel> >& vertices_indexed) {


    //for each vertex in original tris list
    for (uint32_t v = 0; v < tris.size() ; v++)
    {
        //get original vertex position and tex coord position in vertices array
        int tri_v = tris[v];
        int tex_c = tindices[v];

        //get Point value
        const XtndPoint<Kernel> tri_pnt = XtndPoint<Kernel>(
                vertices[(tri_v*3)+0], 
                vertices[(tri_v*3)+1], 
                vertices[(tri_v*3)+2],
                tcoords[(tex_c*2)+0],
                tcoords[(tex_c*2)+1]
                );

        //compare point with all previous vertices
        //go backwards, neighbours are more likely to be added at the end of the list
        uint32_t vertex_id = vertices_indexed.size();
        for (int32_t p = (vertices_indexed.size() - 1); p >= 0; --p)
        {
          //if a match is found, record index 
          if (XtndPoint<Kernel>::float_safe_equals(tri_pnt, vertices_indexed[p]))
          {
            vertex_id = p;
            break;
          }
        }
        //if no match then add to vertices list
        if (vertex_id == vertices_indexed.size()){
          vertices_indexed.push_back(tri_pnt);
        }
        //store index
        tris_indexed.push_back(vertex_id);
    }

  }

};