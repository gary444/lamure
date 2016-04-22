// Copyright (c) 2014 Bauhaus-Universitaet Weimar
// This Software is distributed under the Modified BSD License, see license.txt.
//
// Virtual Reality and Visualization Research Group 
// Faculty of Media, Bauhaus-Universitaet Weimar
// http://www.uni-weimar.de/medien/vr

#ifndef PRE_REDUCTION_HIERARCHICAL_CLUSTERING_MK2_H_
#define PRE_REDUCTION_HIERARCHICAL_CLUSTERING_MK2_H_

#include <lamure/pre/reduction_strategy.h>
#include <lamure/pre/surfel.h>
#include <lamure/pre/bvh.h>

namespace lamure {
namespace pre {

struct hierarchical_cluster_mk2
{
	std::vector<surfel*> surfels;
	
	vec3r_t centroid_pos;
	vec3r_t centroid_normal;
	vec3r_t centroid_color;

	vec3f_t normal_pos;
	vec3f_t normal_normal;
	vec3f_t normal_color;
	
	real_t variation_pos;
	real_t variation_normal;
	real_t variation_color;
};



struct cluster_comparator_mk2
{
  bool operator()(const hierarchical_cluster_mk2& lhs, const hierarchical_cluster_mk2& rhs) const
  {
    if(lhs.surfels.size() != rhs.surfels.size())
	{
		return lhs.surfels.size() < rhs.surfels.size();
	}
	else
	{
		real_t total_variation_left = lhs.variation_pos + lhs.variation_normal + lhs.variation_color;
		real_t total_variation_right = rhs.variation_pos + rhs.variation_normal + rhs.variation_color;
		return total_variation_left < total_variation_right;
	}
  }
};



class PREPROCESSING_DLL reduction_hierarchical_clustering_mk2 : public reduction_strategy
{
public:

	explicit reduction_hierarchical_clustering_mk2();

    surfel_mem_array create_lod(real_t& reduction_error,
    							const std::vector<surfel_mem_array*>& input,
                                const uint32_t surfels_per_node,
          						const bvh& tree,
          						const size_t start_node_id) const override;

private:

	std::vector<std::vector<surfel*>> split_point_cloud(const std::vector<surfel*>& input_surfels, uint32_t max_cluster_size, real_t max_variation, const uint32_t& max_clusters) const;

	hierarchical_cluster_mk2 calculate_cluster_data(const std::vector<surfel*>& input_surfels) const;

	real_t calculate_variation(const mat3d_t& covariance_matrix, vec3f_t& normal) const;

	mat3d_t calculate_covariance_matrix(const std::vector<surfel*>& surfels_to_sample, vec3r_t& centroid) const;

	mat3d_t calculate_covariance_matrix_normal(const std::vector<surfel*>& surfels_to_sample, vec3r_t& centroid) const;

	mat3d_t calculate_covariance_matrix_color(const std::vector<surfel*>& surfels_to_sample, vec3r_t& centroid) const;

	vec3r_t calculate_centroid(const std::vector<surfel*>& surfels_to_sample) const;

	vec3r_t calculate_centroid_normal(const std::vector<surfel*>& surfels_to_sample) const;

	vec3r_t calculate_centroid_color(const std::vector<surfel*>& surfels_to_sample) const;

	surfel create_surfel_from_cluster(const std::vector<surfel*>& surfels_to_sample) const;

	real_t point_plane_distance(const vec3r_t& centroid, const vec3f_t& normal, const vec3r_t& point) const;

	void jacobi_rotation(const mat3d_t& _matrix, double* eigenvalues, double** eigenvectors) const;

	void eigsrt_jacobi(int dim, double* eigenvalues, double** eigenvectors) const;
};

} // namespace pre
} // namespace lamure

#endif // PRE_REDUCTION_HIERARCHICAL_CLUSTERING_MK2_H_
