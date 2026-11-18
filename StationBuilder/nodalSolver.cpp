#include "nodalSolver.h"
#include <Eigen/Sparse>
#include <Eigen/SparseLU>

template <typename T>
struct nodalSolver<T>::implementation
{
	using luSolver = Eigen::SparseLU<Eigen::SparseMatrix<T>, Eigen::COLAMDOrdering<int>>;
	int nodes = 0;
	std::vector<Eigen::Triplet<T>> entries;
	std::unique_ptr<luSolver> lu;   //SparseLU can't be copied or moved, so it lives on the heap
};

template <typename T>
nodalSolver<T>::nodalSolver()
	: data(std::make_unique<implementation>())
{
}

template <typename T>
nodalSolver<T>::~nodalSolver() = default;

template <typename T>
void nodalSolver<T>::reset(int _nodeCount)
{
	data->nodes = _nodeCount;
	data->entries.clear();
	data->lu.reset();
}

template <typename T>
int nodalSolver<T>::nodeCount() const
{
	return data->nodes;
}

template <typename T>
void nodalSolver<T>::addBranch(int _a, int _b, T _g)
{
	data->entries.emplace_back(_a, _a, _g);
	data->entries.emplace_back(_b, _b, _g);
	data->entries.emplace_back(_a, _b, -_g);
	data->entries.emplace_back(_b, _a, -_g);
}

template <typename T>
void nodalSolver<T>::addShunt(int _n, T _g)
{
	data->entries.emplace_back(_n, _n, _g);
}

template <typename T>
void nodalSolver<T>::addEntry(int _row, int _column, T _value)
{
	data->entries.emplace_back(_row, _column, _value);
}

template <typename T>
bool nodalSolver<T>::factorise()
{
	Eigen::SparseMatrix<T> matrix(data->nodes, data->nodes);
	matrix.setFromTriplets(data->entries.begin(), data->entries.end());   //repeated entries are added together
	matrix.makeCompressed();
	data->lu = std::make_unique<typename implementation::luSolver>();
	data->lu->compute(matrix);
	if (data->lu->info() != Eigen::Success)
	{
		data->lu.reset();
		return false;
	}
	return true;
}

template <typename T>
bool nodalSolver<T>::isFactorised() const
{
	return data->lu != nullptr;
}

template <typename T>
bool nodalSolver<T>::solve(const vector& _injections, vector& _x) const
{
	if (!data->lu || int(_injections.size()) != data->nodes)
		return false;
	const Eigen::Map<const Eigen::Matrix<T, Eigen::Dynamic, 1>> b(_injections.data(), data->nodes);
	const Eigen::Matrix<T, Eigen::Dynamic, 1> x = data->lu->solve(b);
	if (data->lu->info() != Eigen::Success)
		return false;
	_x.assign(x.data(), x.data() + data->nodes);
	return true;
}

template class nodalSolver<double>;
template class nodalSolver<std::complex<double>>;
