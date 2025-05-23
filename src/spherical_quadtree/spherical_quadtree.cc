#include "spherical_quadtree/spherical_quadtree.h"

#include <random>

#include "slam6d/globals.icc"

std::mt19937 gen(0);

#ifndef _MSC_VER
// needed until we compile with C++14
#if __cplusplus >= 201402L
//#error remove definition of make_unique
#endif
namespace std
{
/*
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args &&... args)
{
	return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
*/
} // namespace std
#endif

static void circumcircle(const double *v1, const double *v2, const double *v3,
			 double *p, double *theta)
{
	double a[3] = {v1[0] - v3[0], v1[1] - v3[1], v1[2] - v3[2]};
	double b[3] = {v2[0] - v3[0], v2[1] - v3[1], v2[2] - v3[2]};
	double c[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
	double la = Len(a);
	double la2 = sqr(la);
	double lb = Len(b);
	double lb2 = sqr(lb);
	double axb[3];
	Cross(a, b, axb);
	double laxb = Len(axb);
	double laxb2 = 2 * sqr(laxb);
	double r = (la * lb * Len(c)) / (2 * laxb);
	/*
	 * Now we have the radius of the circumcircle but in the end, instead
	 * of the radius, we want half the angle under which the circumcircle
	 * is seen from the center of the sphere. In other words: instead of
	 * the radius of the base of the spherical cap, we want the angle theta
	 */
	*theta = asin(r);
	double sa[3] = {lb2 * a[0], lb2 * a[1], lb2 * a[2]};
	double sb[3] = {la2 * b[0], la2 * b[1], la2 * b[2]};
	double di[3] = {sb[0] - sa[0], sb[1] - sa[1], sb[2] - sa[2]};
	Cross(di, axb, p);
	p[0] = p[0] / laxb2 + v3[0];
	p[1] = p[1] / laxb2 + v3[1];
	p[2] = p[2] / laxb2 + v3[2];
	/*
	 * the point p we are interested in is not the center of the base of
	 * the circle cap but the point on the unit-sphere so that we can
	 * easily find the angle between it and other vectors later
	 */
	Normalize3(p);
}

static double tripleproduct(const double *a, const double *b, const double *c)
{
	double tmp[3];
	Cross(a, b, tmp);
	return tmp[0] * c[0] + tmp[1] * c[1] + tmp[2] * c[2];
}

static size_t
middle(size_t a, size_t b, std::vector<std::array<double, 3>> &vertices,
       std::unordered_map<std::pair<size_t, size_t>, size_t> &middlemap)
{
	std::pair<size_t, size_t> key = std::make_pair(a, b);
	std::unordered_map<std::pair<size_t, size_t>, size_t>::const_iterator
	    it = middlemap.find(key);
	if (it != middlemap.end()) {
		return it->second;
	}
	double norm[3] = {
	    (vertices[a][0] + vertices[b][0]) / 2,
	    (vertices[a][1] + vertices[b][1]) / 2,
	    (vertices[a][2] + vertices[b][2]) / 2,
	};
	Normalize3(norm);
	vertices.push_back({norm[0], norm[1], norm[2]});
	size_t i = vertices.size() - 1;
	middlemap[key] = i;
	return i;
}

QuadNode::QuadNode(
    size_t v1, size_t v2, size_t v3, std::vector<size_t> const &_indices,
    std::vector<std::array<double, 3>> const &_pts,
    std::vector<std::array<double, 3>> &vertices,
    std::unordered_map<std::pair<size_t, size_t>, size_t> &middlemap)
    : pts(_pts)
{
	double w1[3] = {vertices[v1][0], vertices[v1][1], vertices[v1][2]};
	double w2[3] = {vertices[v2][0], vertices[v2][1], vertices[v2][2]};
	double w3[3] = {vertices[v3][0], vertices[v3][1], vertices[v3][2]};
	circumcircle(w1, w2, w3, ccp, &ccr);
	// also compute the area of the triangle on the sphere surface
	//    A = R²(alpha+beta+gamma-π)
	// The angles are the angles between the planes from sphere center to
	// each side of the triangle
	double a[3] = {w1[0] - w2[0], w1[1] - w2[1], w1[2] - w2[2]};
	double b[3] = {w2[0] - w3[0], w2[1] - w3[1], w2[2] - w3[2]};
	double c[3] = {w3[0] - w1[0], w3[1] - w1[1], w3[2] - w1[2]};
	double A[3], B[3], C[3];
	Cross(a, w1, A);
	Cross(b, w2, B);
	Cross(c, w3, C);
	Normalize3(A);
	Normalize3(B);
	Normalize3(C);
	double alpha = M_PI - acos(A[0] * B[0] + A[1] * B[1] + A[2] * B[2]);
	double beta = M_PI - acos(B[0] * C[0] + B[1] * C[1] + B[2] * C[2]);
	double gamma = M_PI - acos(C[0] * A[0] + C[1] * A[1] + C[2] * A[2]);
	area = (alpha + beta + gamma - M_PI);
	// we stop producing child nodes if we have less than 100 points for
	// this node
	//
	// in cases where the same point exists more than 100 times, we would
	// recurse forever, so we also limit the size of the circumcircle
	if (_indices.size() <= 100 || ccr < 1e-10) {
		// leaf
		indices.insert(indices.end(), _indices.begin(), _indices.end());
		isleaf = true;
		return;
	}
	isleaf = false;
	size_t v4 = middle(v1, v2, vertices, middlemap);
	size_t v5 = middle(v2, v3, vertices, middlemap);
	size_t v6 = middle(v3, v1, vertices, middlemap);
	double w4[3] = {vertices[v4][0], vertices[v4][1], vertices[v4][2]};
	double w5[3] = {vertices[v5][0], vertices[v5][1], vertices[v5][2]};
	double w6[3] = {vertices[v6][0], vertices[v6][1], vertices[v6][2]};
	std::vector<size_t> indices1, indices2, indices3, indices4;
	for (size_t i : _indices) {
		double p[3] = {_pts[i][0], _pts[i][1], _pts[i][2]};
		/*
		 * this version is nearly functionally identical to the more
		 * computationally expensive version below except that the rare
		 * situation that a point does not fit into any of the four
		 * triangles due to floating point inaccuracy cannot arise
		 * anymore.
		 */
		if (tripleproduct(w4, w6, p) >= 0) {
			indices1.push_back(i);
		} else if (tripleproduct(w5, w4, p) >= 0) {
			indices2.push_back(i);
		} else if (tripleproduct(w6, w5, p) >= 0) {
			indices3.push_back(i);
		} else {
			indices4.push_back(i);
		}
		/*
		if (tripleproduct(w1,w4,p) >= 0 && tripleproduct(w4,w6,p) >= 0
		&& tripleproduct(w6,w1,p) >= 0) { indices1.push_back(i); } else
		if (tripleproduct(w2,w5,p) >= 0 && tripleproduct(w5,w4,p) >= 0
		&& tripleproduct(w4,w2,p) >= 0) { indices2.push_back(i); } else
		if (tripleproduct(w3,w6,p) >= 0 && tripleproduct(w6,w5,p) >= 0
		&& tripleproduct(w5,w3,p) >= 0) { indices3.push_back(i); } else
		if (tripleproduct(w4,w5,p) >= 0 && tripleproduct(w5,w6,p) >= 0
		&& tripleproduct(w6,w4,p) >= 0) { indices4.push_back(i); } else
		{ std::cerr << "impossible for " << p[0] << "," << p[1] << ","
		<< p[2]
		<< std::endl; exit(1);
		}
		*/
	}
	t1 = std::make_unique<QuadNode>(v1, v4, v6, indices1, _pts, vertices,
					middlemap);
	t2 = std::make_unique<QuadNode>(v2, v5, v4, indices2, _pts, vertices,
					middlemap);
	t3 = std::make_unique<QuadNode>(v3, v6, v5, indices3, _pts, vertices,
					middlemap);
	t4 = std::make_unique<QuadNode>(v4, v5, v6, indices4, _pts, vertices,
					middlemap);
}

std::vector<size_t> QuadNode::search(double p[3], const double r) const
{
	if (isleaf) {
		std::vector<size_t> res;
		for (size_t i : indices) {
			double dot = p[0] * pts[i][0] + p[1] * pts[i][1] +
				     p[2] * pts[i][2];
			if (dot >= 1.0) {
				res.push_back(i);
				continue;
			}
			double angle = acos(dot);
			if (angle < r) {
				res.push_back(i);
			}
		}
		return res;
	}
	double dot = p[0] * ccp[0] + p[1] * ccp[1] + p[2] * ccp[2];
	double angle = acos(dot);
	std::vector<size_t> res;
	if (angle > r + ccr) {
		return res;
	}
	if (angle < r - ccr) {
		return getall();
	}
	std::vector<size_t> res1 = t1->search(p, r);
	std::vector<size_t> res2 = t2->search(p, r);
	std::vector<size_t> res3 = t3->search(p, r);
	std::vector<size_t> res4 = t4->search(p, r);
	res.insert(res.begin(), res1.begin(), res1.end());
	res.insert(res.begin(), res2.begin(), res2.end());
	res.insert(res.begin(), res3.begin(), res3.end());
	res.insert(res.begin(), res4.begin(), res4.end());
	return res;
}

std::vector<size_t> QuadNode::reduce(double theta, double cap_area,
				     int numpts) const
{
	if (numpts == 0) {
		throw std::runtime_error("nr of pts per quad cannot be zero");
	}
	if (numpts < 0) {
		throw std::runtime_error(
		    "nr of pts per quad cannot be less than zero");
	}
	std::vector<size_t> res;
	if (isleaf || ccr * 2 < theta) {
		std::vector<size_t> all = getall();
		// the user requested numpts on a sphere cap with area cap_area
		// the current triangle covers less than the surface of that
		// sphere cap compute the number of points we should retrieve
		// from this triangle by computing the ratio between cap_area
		// and triangle area
		double new_numpts = numpts * area / cap_area;
		if (all.size() <= new_numpts) {
			return all;
		}
		// We decide for each point whether it should go into the final
		// list instead of computing the number of points we need.
		// Example why this is useful: suppose a lot of triangles have
		// areas such that we need 0.3 points from them. Then we would
		// get *zero* points for all of them. Since triangle sizes are
		// very similar, this effect doesn't cancel out overall.
		std::bernoulli_distribution d(new_numpts / all.size());
		for (std::vector<size_t>::iterator it = all.begin();
		     it != all.end(); ++it) {
			if (d(gen)) {
				res.push_back(*it);
			}
		}
		return res;
	}
	std::vector<size_t> res1 = t1->reduce(theta, cap_area, numpts);
	std::vector<size_t> res2 = t2->reduce(theta, cap_area, numpts);
	std::vector<size_t> res3 = t3->reduce(theta, cap_area, numpts);
	std::vector<size_t> res4 = t4->reduce(theta, cap_area, numpts);
	res.insert(res.end(), res1.begin(), res1.end());
	res.insert(res.end(), res2.begin(), res2.end());
	res.insert(res.end(), res3.begin(), res3.end());
	res.insert(res.end(), res4.begin(), res4.end());
	return res;
}

std::vector<size_t> QuadNode::getall() const
{
	if (isleaf) {
		return indices;
	}
	std::vector<size_t> res;
	std::vector<size_t> res1 = t1->getall();
	std::vector<size_t> res2 = t2->getall();
	std::vector<size_t> res3 = t3->getall();
	std::vector<size_t> res4 = t4->getall();
	res.insert(res.begin(), res1.begin(), res1.end());
	res.insert(res.begin(), res2.begin(), res2.end());
	res.insert(res.begin(), res3.begin(), res3.end());
	res.insert(res.begin(), res4.begin(), res4.end());
	return res;
}

QuadTree::QuadTree(DataXYZ const &_pts)
{
	for (size_t i = 0; i < _pts.size(); ++i) {
		double norm[3] = {_pts[i][0], _pts[i][1], _pts[i][2]};
		Normalize3(norm);
		pts.push_back({norm[0], norm[1], norm[2]});
	}
	/*
	 * create a list containing the eight sides of the octahedron
	 *
	 * we choose the octahedron because it is trivial to check whether a
	 * point falls into one of the faces by aligning the octahendron with
	 * the coordinate axis (it then boils down to a check of the signs) and
	 * because the fewer faces, the fewer triangle checks have to be done
	 * to figure out into which face a point falls
	 */
	vertices.push_back({-1, 0, 0});
	vertices.push_back({1, 0, 0});
	vertices.push_back({0, -1, 0});
	vertices.push_back({0, 1, 0});
	vertices.push_back({0, 0, -1});
	vertices.push_back({0, 0, 1});
	std::vector<std::array<size_t, 3>> mainvertices;
	for (int x : {-1, 1}) {
		for (int y : {-1, 1}) {
			for (int z : {-1, 1}) {
				size_t v1 = x < 0 ? 0 : 1;
				size_t v2 = y < 0 ? 2 : 3;
				size_t v3 = z < 0 ? 4 : 5;
				// make sure that the vertices of the triangle
				// are given in the same order - i.e. make sure
				// that the normal vector always points outward
				if (((x > 0) ^ (y > 0) ^ (z > 0)) == false) {
					std::swap(v1, v3);
				}
				mainvertices.push_back({v1, v2, v3});
			}
		}
	}
	std::array<std::vector<size_t>, 8> buckets;
	for (size_t i = 0; i < pts.size(); ++i) {
		size_t idx = (int)(pts[i][0] > 0) << 2 |
			     (int)(pts[i][1] > 0) << 1 | (int)(pts[i][2] > 0);
		buckets[idx].push_back(i);
	}
	for (int i = 0; i < 8; ++i) {
		trees[i] = std::make_unique<QuadNode>(
		    mainvertices[i][0], mainvertices[i][1], mainvertices[i][2],
		    buckets[i], pts, vertices, middlemap);
	}
}

std::vector<size_t> QuadTree::search(double p[3], const double r) const
{
	std::vector<size_t> result;
	for (std::unique_ptr<QuadNode> const &n : trees) {
		std::vector<size_t> res = n->search(p, r);
		result.insert(result.end(), res.begin(), res.end());
	}
	return result;
}

std::vector<size_t> QuadTree::reduce(double theta, int numpts) const
{
	if (numpts == 0) {
		throw std::runtime_error("nr of pts per quad cannot be zero");
	}
	if (numpts < 0) {
		throw std::runtime_error(
		    "nr of pts per quad cannot be less than zero");
	}
	// compute area of sphere cap under angle theta
	double cap_area = 2 * M_PI * (1 - cos(theta));
	std::vector<size_t> result;
	for (std::unique_ptr<QuadNode> const &n : trees) {
		std::vector<size_t> res = n->reduce(theta, cap_area, numpts);
		result.insert(result.end(), res.begin(), res.end());
	}
	return result;
}
