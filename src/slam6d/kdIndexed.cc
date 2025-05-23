/*
 * kd implementation
 *
 * Copyright (C) by the 3DTK contributors
 *
 * Released under the GPL version 3.
 *
 */

/** @file
 *  @brief An optimized k-d tree implementation
 *  @author Remus Dumitru. Jacobs University Bremen, Germany
 *  @author Corneliu-Claudiu Prodescu. Jacobs University Bremen, Germany
 *  @author Andreas Nuechter. Jacobs University Bremen, Germany.
 *  @author Kai Lingemann. Inst. of CS, University of Osnabrueck, Germany.
 *  @author Thomas Escher Inst. of CS, University of Osnabrueck, Germany.
 */

#ifdef _MSC_VER
#define  _USE_MATH_DEFINES
#endif

#include "slam6d/kdIndexed.h"
#include "slam6d/globals.icc"

#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

// KDtree class static variables
template<class PointData, class AccessorData, class AccessorFunc, class PointType, class ParamFunc>
KDParams<PointType> KDTreeImpl<PointData, AccessorData, AccessorFunc, PointType, ParamFunc>::params[MAX_OPENMP_NUM_THREADS];

/**
 * Constructor
 *
 * Create a KD tree from the points pointed to by the array pts
 *
 * @param pts 3D array of points
 * @param n number of points
 */
KDtreeIndexed::KDtreeIndexed(double **pts, size_t n, int bucketSize)
{
    m_data = pts;
    m_size = n;
    create(pts, prepareTempIndices(n), n, bucketSize);
    delete[] m_temp_indices;
}

size_t* KDtreeIndexed::prepareTempIndices(size_t n)
{
    m_temp_indices = new size_t[n];
    for(unsigned int i = 0; i < n; ++i)
        m_temp_indices[i] = i;
    return m_temp_indices;
}

KDtreeIndexed::~KDtreeIndexed()
{
}

size_t KDtreeIndexed::getNrPts() const
{
    return m_size;
}

std::vector<size_t> KDtreeIndexed::CollectPts(int threadNum) const
{
    params[threadNum].collected_pts.clear();
    _CollectPts(m_data, threadNum);
    return params[threadNum].collected_pts;
}

int KDtreeIndexed::Remove(double *_p,
                            int threadNum)
{
    // We might delete a point.
    if (m_size > 0) {
        params[threadNum].closest = 0;
        params[threadNum].closest_d2 = __DBL_MAX__;
        params[threadNum].p = _p;
        int removed = _Remove(m_data, threadNum);
        m_size -= removed;
        return removed;
    }
    // We delete no point.
    return 0;
}

/**
 * Finds the closest point within the tree,
 * wrt. the point given as first parameter.
 * @param _p point
 * @param maxdist2 maximal search distance.
 * @param threadNum Thread number, for parallelization
 * @return Pointer to the closest point
 */
size_t KDtreeIndexed::FindClosest(double *_p,
                            double maxdist2,
                            int threadNum) const
{
  params[threadNum].closest = std::numeric_limits<size_t>::max();
  params[threadNum].closest_d2 = maxdist2;
  params[threadNum].p = _p;
  _FindClosest(m_data, threadNum);
  return params[threadNum].closest;
}

size_t KDtreeIndexed::FindClosestAlongDir(double *_p,
                                    double *_dir,
                                    double maxdist2,
                                    int threadNum) const
{
  params[threadNum].closest = std::numeric_limits<size_t>::max();
  params[threadNum].closest_d2 = maxdist2;
  params[threadNum].p = _p;
  params[threadNum].dir = _dir;
  _FindClosestAlongDir(m_data, threadNum);
  return params[threadNum].closest;
}

std::vector<size_t> KDtreeIndexed::kNearestNeighbors(double *_p,
                                        int _k,
                                        int threadNum) const
{
  std::vector<size_t> result;
  params[threadNum].closest = 0;
  params[threadNum].p = _p;
  params[threadNum].k = _k;
  // todo fix this C/C++ mixture
  // FIXME: in kdTreeImpl.h elements of closest_neighbors are compared with
  // NULL to find out whether they are set or not. This practice doesn't work
  // for the indexed kd-tree!!
  // A solution would be to default the members of closest_neighbors to
  // std::numeric_limits<size_t>::max() and check against that in kdTreeImpl.h
  // or by checking the special distance value of -1.0 in the distances
  // vector.
  params[threadNum].closest_neighbors = (size_t*)calloc(_k,
                                                          sizeof(size_t) );
  params[threadNum].distances = (double *)calloc(_k,
                                                 sizeof(double));
   // initialize distances to an invalid value to indicate unset neighbors
  for (int i = 0; i < _k; i++) {
      params[threadNum].distances[i] = -1.0;
  }
  _KNNSearch(m_data, threadNum);

  for (int i = 0; i < _k; i++) {
    if (params[threadNum].distances[i] >= 0.0f) {
    result.push_back(params[threadNum].closest_neighbors[i]);
    }
  }

  free (params[threadNum].closest_neighbors);
  free (params[threadNum].distances);

  return result;
}


std::vector<size_t> KDtreeIndexed::fixedRangeSearchBetween2Points(double *_p,
                      double *_p0,
                      double maxdist2,
                      int threadNum) const {
  std::vector<size_t> result;
  params[threadNum].p0 = _p0;
  params[threadNum].closest_d2 = maxdist2;
  params[threadNum].p = _p;
  params[threadNum].dist = sqrt(Dist2(_p, _p0));

  double * _dir = new double[3];
  for(int i = 0; i < 3; i++) {
    _dir[i] = _p0[i] - _p[i];
  }

  Normalize3(_dir);

  params[threadNum].dir = _dir;
  params[threadNum].range_neighbors.clear();

  _fixedRangeSearchBetween2Points(m_data, threadNum);

  for (size_t i = 0; i < params[threadNum].range_neighbors.size(); i++) {
    result.push_back(params[threadNum].range_neighbors[i]);
  }

  delete[] _dir;
  return result;
}


std::vector<size_t> KDtreeIndexed::fixedRangeSearchAlongDir(double *_p,
                      double *_dir,
                      double maxdist2,
                      int threadNum) const {
  std::vector<size_t> result;
  params[threadNum].closest = 0;
  params[threadNum].closest_d2 = maxdist2;
  params[threadNum].p = _p;
  params[threadNum].dir = _dir;
  params[threadNum].range_neighbors.clear();

  _fixedRangeSearchAlongDir(m_data, threadNum);

  for (size_t i = 0; i < params[threadNum].range_neighbors.size(); i++) {
    result.push_back(params[threadNum].range_neighbors[i]);
  }

  return result;
}

std::vector<size_t> KDtreeIndexed::fixedRangeSearch(double *_p,
                                       double sqRad2,
                                       int threadNum) const
{
  std::vector<size_t> result;
  params[threadNum].closest = 0;
  params[threadNum].closest_d2 = sqRad2;
  params[threadNum].p = _p;
  params[threadNum].range_neighbors.clear();
  _FixedRangeSearch(m_data, threadNum);

  for (size_t i = 0; i < params[threadNum].range_neighbors.size(); i++) {
    result.push_back(params[threadNum].range_neighbors[i]);
  }

  return result;
}

std::vector<size_t> KDtreeIndexed::AABBSearch(double *_p,
                                       double* _p0,
                                       int threadNum) const
{
    if (_p[0] > _p0[0] || _p[1] > _p0[1] || _p[2] > _p0[2])
        throw std::logic_error("invalid bbox");
  std::vector<size_t> result;
  params[threadNum].p = _p;
  params[threadNum].p0 = _p0;
  params[threadNum].range_neighbors.clear();
  _AABBSearch(m_data, threadNum);

  for (size_t i = 0; i < params[threadNum].range_neighbors.size(); i++) {
    result.push_back(params[threadNum].range_neighbors[i]);
  }

  return result;
}

std::vector<size_t> KDtreeIndexed::segmentSearch_all(double *_p, double* _p0, double maxdist2, int threadNum) const
{
  std::vector<size_t> result;
  params[threadNum].maxdist_d2 = maxdist2;
  params[threadNum].maxdist_d = sqrt(maxdist2);
  params[threadNum].p = _p;
  params[threadNum].p0 = _p0;
  params[threadNum].range_neighbors.clear();
  double *dir = new double[3]{_p0[0] - _p[0], _p0[1] - _p[1], _p0[2] - _p[2] };
  double len2 = Len2(dir);
  double *n = new double[3]{dir[0]/len2,dir[1]/len2,dir[2]/len2};
  double *center = new double[3]{_p[0]+dir[0]*0.5, _p[1]+dir[1]*0.5, _p[2]+dir[2]*0.5};
  double r2 = sqr(0.5*sqrt(len2)+sqrt(maxdist2));
  params[threadNum].segment_dir = dir;
  params[threadNum].segment_len2 = len2;
  params[threadNum].segment_n = n;
  params[threadNum].segment_center = center;
  params[threadNum].segment_r2 = r2;
  _segmentSearch_all(m_data, threadNum);
  for (size_t i = 0; i < params[threadNum].range_neighbors.size(); i++) {
    result.push_back(params[threadNum].range_neighbors[i]);
  }
  delete[] dir;
  delete[] n;
  delete[] center;
  return result;
}

size_t KDtreeIndexed::segmentSearch_1NearestPoint(double *_p, double* _p0, double maxdist2, int threadNum) const
{
  params[threadNum].closest = std::numeric_limits<size_t>::max();
  // the furthest a point can be away is the distance between the points
  // making the line segment plus maxdist
  params[threadNum].closest_d2 = sqr(sqrt(Dist2(_p,_p0))+sqrt(maxdist2));
  //params[threadNum].closest_d2 = 10000000000000;
  params[threadNum].maxdist_d2 = maxdist2;
  params[threadNum].maxdist_d = sqrt(maxdist2);
  params[threadNum].p = _p;
  params[threadNum].p0 = _p0;
  double *dir = new double[3]{_p0[0] - _p[0], _p0[1] - _p[1], _p0[2] - _p[2] };
  double len2 = Len2(dir);
  double *n = new double[3]{dir[0]/len2,dir[1]/len2,dir[2]/len2};
  params[threadNum].segment_dir = dir;
  params[threadNum].segment_len2 = len2;
  params[threadNum].segment_n = n;
  _segmentSearch_1NearestPoint(m_data, threadNum);
  delete[] dir;
  delete[] n;
  return params[threadNum].closest;
}
