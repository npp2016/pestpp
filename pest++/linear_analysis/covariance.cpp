
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <random>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/IterativeLinearSolvers>
#include <Eigen/SparseCholesky>

#include "Pest.h"
#include "utilities.h"
#include "covariance.h"

using namespace std;

//---------------------------------------
//Mat constructors
//---------------------------------------

Mat::Mat(string filename)
{
	//todo: load from filename -> .jco/.jcb or .mat or...throw on .pst
	pest_utils::upper_ip(filename);
	if ((filename.find(".JCO") != string::npos) || (filename.find(".JCB")))
		from_binary(filename);
	else if (filename.find(".MAT") != string::npos)
		from_ascii(filename);
	else
		throw runtime_error("Mat::Mat() error: only .jco/.jcb or .mat\
							 files can be used to instatiate a Mat");

}


Mat::Mat(vector<string> _row_names, vector<string> _col_names, 
	Eigen::SparseMatrix<double> _matrix,bool _autoalign)
{
	row_names = _row_names;
	col_names = _col_names;
	assert(row_names.size() == _matrix.rows());
	assert(col_names.size() == _matrix.cols());
	matrix = _matrix;
	autoalign = _autoalign;
}

Mat::Mat(vector<string> _row_names, vector<string> _col_names, 
	Eigen::SparseMatrix<double> _matrix, MatType _mattype, bool _autoalign)
{
	row_names = _row_names;
	col_names = _col_names;
	assert(row_names.size() == _matrix.rows());
	assert(col_names.size() == _matrix.cols());
	matrix = _matrix;
	mattype = _mattype;
	autoalign = _autoalign;
}

//--------------------------------------
//Mat convience functions
//--------------------------------------
const Eigen::SparseMatrix<double>* Mat::get_matrix_ptr()
{
	const Eigen::SparseMatrix<double>* ptr = &matrix;
	return ptr;
}

const Eigen::SparseMatrix<double>* Mat::get_U_ptr()
{
	if (U.rows() == 0)
	{
		SVD();
	}
	const Eigen::SparseMatrix<double>* ptr = &U;
	return ptr;
}

const Eigen::SparseMatrix<double>* Mat::get_V_ptr()
{
	if (V.rows() == 0)
	{
		SVD();
	}
	const Eigen::SparseMatrix<double>* ptr = &V;
	return ptr;
}

const Eigen::VectorXd* Mat::get_s_ptr()
{
	if (s.size() == 0)
	{
		SVD();
	}
	const Eigen::VectorXd* ptr = &s;
	return ptr;
}

Mat Mat::get_U()
{
	if (U.rows() == 0) SVD();
	vector<string> u_col_names;
	stringstream ss;
	for (int i = 0; i < nrow(); i++)
	{
	ss.clear();
	ss.str(string());
	ss << "left_sing_vec_";
	ss << i + 1;
	u_col_names.push_back(ss.str());
	}
	return Mat(row_names, u_col_names, U, MatType::DENSE,false);
}

Mat Mat::get_V()
{
	if (V.rows() == 0) SVD();
	vector<string> v_col_names;
	stringstream ss;
	for (int i = 0; i < ncol(); i++)
	{
		ss.clear();
		ss.str(string());
		ss << "right_sing_vec_";
		ss << i + 1;
		v_col_names.push_back(ss.str());
	}
	return Mat(col_names, v_col_names, V, MatType::DENSE,false);
}


Mat Mat::get_s()
{
	if (V.rows() == 0) SVD();
	vector<string> s_names;
	vector<Eigen::Triplet<double>> triplet_list;
	stringstream ss;
	for (int i = 0; i < s.size(); i++)
	{
		ss.clear();
		ss.str(string());
		ss << "sing_val_";
		ss << i + 1;
		s_names.push_back(ss.str());
		triplet_list.push_back(Eigen::Triplet<double>(i, i, s[i]));
	}
	Eigen::SparseMatrix<double> s_mat(s.size(),s.size());
	s_mat.setZero();
	s_mat.setFromTriplets(triplet_list.begin(), triplet_list.end());
	return Mat(s_names, s_names, s_mat , MatType::DIAGONAL,false);
}

Mat Mat::transpose()
{
	return Mat(col_names, row_names, matrix.transpose());
}

Mat Mat::T()
{
	return Mat(col_names, row_names, matrix.transpose());
}

void Mat::transpose_ip()
{
	if (mattype != MatType::DIAGONAL)
	{
		matrix = matrix.transpose();
		vector<string> temp = row_names;
		row_names = col_names;
		col_names = temp;
	}
}

Mat Mat::inv()
{
	if (nrow() != ncol()) throw runtime_error("Mat::inv() error: only symmetric positive definite matrices can be inverted with Mat::inv()");
	if (mattype == MatType::DIAGONAL)
	{
		cout << "diagonal" << endl;
		Eigen::VectorXd diag = matrix.diagonal();
		diag = 1.0 / diag.array();
		vector<Eigen::Triplet<double>> triplet_list;
		
		for (int i = 0; i != diag.size(); ++i)
		{
			triplet_list.push_back(Eigen::Triplet<double>(i, i, diag[i]));
		}
		Eigen::SparseMatrix<double> inv_mat(triplet_list.size(),triplet_list.size());
		inv_mat.setZero();
		inv_mat.setFromTriplets(triplet_list.begin(), triplet_list.end());

		return Mat(row_names, col_names, inv_mat,MatType::DIAGONAL);
	}
	
	//Eigen::ConjugateGradient<Eigen::SparseMatrix<double>> solver;
	Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
	solver.compute(matrix);
	Eigen::SparseMatrix<double> I(nrow(), nrow());
	I.setIdentity();
	Eigen::SparseMatrix<double> matrix_inv = solver.solve(I);
	return Mat(row_names, col_names, matrix_inv, MatType::DENSE,autoalign);
}

void Mat::SVD()
{
	Eigen::JacobiSVD<Eigen::MatrixXd> svd_fac(matrix, Eigen::DecompositionOptions::ComputeFullU | Eigen::DecompositionOptions::ComputeFullV);
	s = svd_fac.singularValues();
	U = svd_fac.matrixU().sparseView();
	V = svd_fac.matrixV().sparseView();
}



//---------------------------------------
//Mat operator
//--------------------------------------

ostream& operator<< (ostream &os, Mat mat)
{
	cout << "row names : ";
	for (auto &name : mat.get_row_names())
		cout << name << ',';
	cout << endl << "col names : ";
	for (auto &name : mat.get_col_names())
		cout << name << ',';
	cout << endl;
	cout << *mat.get_matrix_ptr();
	return os;
}

Mat Mat::operator+(Mat &other_mat)
{
	vector<string> common_rows, common_cols;
	MatType new_mattype = MatType::DENSE;
	if ((mattype == MatType::DIAGONAL) && (other_mat.get_mattype() == MatType::DIAGONAL))
		new_mattype = MatType::DIAGONAL;
	//if someone isn't wanting to autoalign, then just check the dimensions
	if ((!autoalign) || (!other_mat.get_autoalign()))
	{
		if ((nrow() != other_mat.nrow()) || (ncol() != other_mat.ncol()))
		{
			stringstream ss;
			ss << "Mat objects not aligned for '+': (" << nrow() << ',' << ncol();
			ss << ") | (" << other_mat.nrow() << ',' << other_mat.ncol() << ')';
			throw runtime_error("Mat::operator+ss() error:" +ss.str());
		}
		return Mat(row_names, col_names, matrix + *other_mat.get_matrix_ptr(), MatType::DENSE, false);
	}

	//auto align for element-wise addition
	for (auto &name : other_mat.get_col_names())
		if (find(col_names.begin(), col_names.end(), name) != col_names.end())
			common_cols.push_back(name);
	for (auto &name : other_mat.get_row_names())
		if (find(row_names.begin(), row_names.end(), name) != row_names.end())
			common_rows.push_back(name);
	if (common_cols.size() == 0) throw runtime_error("Mat::operator+() error: no common cols found");
	if (common_rows.size() == 0) throw runtime_error("Mat::operator+() error: no common rows found");
	//no realignment needed
	if ((common_cols == col_names) && (common_cols == other_mat.get_col_names())
		&& (common_rows == row_names) && (common_rows == other_mat.get_row_names()))
		return Mat(row_names, col_names, matrix + *other_mat.get_matrix_ptr(), new_mattype);

	//only other_mat needs realignment
	else if ((common_rows == row_names) && (common_cols == col_names))
	{
		Mat new_other_mat = other_mat.get(common_rows, common_cols);
		return Mat(common_rows, common_cols, matrix + *new_other_mat.get_matrix_ptr(), new_mattype);
	}
	//only this needs realignment
	else if ((common_rows == other_mat.get_row_names()) && (common_cols == other_mat.get_col_names()))
	{
		Mat new_this = get(common_rows, common_cols);
		return Mat(common_rows, common_cols, *new_this.get_matrix_ptr() + *other_mat.get_matrix_ptr(), new_mattype);
	}
	else
	{
		Mat new_other_mat = other_mat.get(common_rows, common_cols);
		Mat new_this = get(common_rows, common_cols);
		return Mat(common_rows, common_cols, *new_this.get_matrix_ptr() + *new_other_mat.get_matrix_ptr(), new_mattype);
	}
}

Mat Mat::operator*(double val)
{
	return Mat(row_names, col_names, matrix*val);
}

Mat Mat::operator*(Mat &other_mat)
{
	
	MatType new_mattype = MatType::DENSE;
	if ((mattype == MatType::DIAGONAL) && (other_mat.get_mattype() == MatType::DIAGONAL))
		new_mattype = MatType::DIAGONAL;
	
	if ((!autoalign) || (!other_mat.get_autoalign()))
	{
		if (nrow() != other_mat.ncol())
		{
			stringstream ss;
			ss << "Mat objects not aligned for '*': (" << nrow() << ',' << ncol();
			ss << ") | (" << other_mat.nrow() << ',' << other_mat.ncol() << ')';
			throw runtime_error("Mat::operator*() error: "+ss.str());
		}
		return Mat(row_names, other_mat.get_col_names(), matrix * *other_mat.get_matrix_ptr(), MatType::DENSE, false);
	}

	vector<string> common;
	for (auto &row_name : other_mat.get_row_names())
	{
		if (find(col_names.begin(), col_names.end(), row_name) != col_names.end())
			common.push_back(row_name);
	}
	if (common.size() == 0)
	{
		cout << "no common this.col_names/other_mat.row_names:" << endl << "this.col_names:" << endl;
		for (auto &name : col_names) cout << name << ',';
		cout << endl << "other_mat.row_names:" << endl;
		for (auto &name : other_mat.get_row_names()) cout << name << ',';
		cout << endl;
		throw runtime_error("Mat::operator+() error: no common elements found in Mat::operator*");
	}
	//no realignment needed...
	if ((common == col_names) && (common == other_mat.get_row_names()))
	{
		Eigen::SparseMatrix<double> new_matrix = matrix * *other_mat.get_matrix_ptr();
		Mat new_mat(row_names, other_mat.col_names, new_matrix, new_mattype);
		return new_mat;
	}

	//only other_mat needs realignment
	else if (common == col_names)
	{
		Mat new_other_mat = other_mat.get(common, other_mat.get_col_names());
		Eigen::SparseMatrix<double> new_matrix = matrix * *new_other_mat.get_matrix_ptr();
		Mat new_mat(row_names, new_other_mat.get_col_names(), new_matrix, new_mattype);
		return new_mat;
	}
	
	//only this needs realignment
	else if (common == other_mat.get_row_names())
	{
		Mat new_this = get(row_names, common);
		Eigen::SparseMatrix<double> new_matrix = *new_this.get_matrix_ptr() * *other_mat.get_matrix_ptr();
		Mat new_mat(new_this.get_row_names(), other_mat.col_names, new_matrix, new_mattype);
		return new_mat;

	}
	//both need realignment
	else
	{
		Mat new_other_mat = other_mat.get(common, other_mat.get_col_names());
		Mat new_this = get(row_names, common);
		Eigen::SparseMatrix<double> new_matrix = *new_this.get_matrix_ptr() * *new_other_mat.get_matrix_ptr();
		Mat new_mat(new_this.get_col_names(),new_other_mat.get_row_names(),new_matrix, new_mattype);
		return new_mat;
	}


}



//-----------------------------------------
//Mat IO
//-----------------------------------------
void Mat::to_ascii(const string &filename)
{
	ofstream out(filename);
	if (!out.good())
	{
		throw runtime_error("Mat::to_ascii() error: cannot open " + filename + "\
													 to write ASCII matrix");
	}
	out << setw(6) << nrow() << setw(6) << ncol() << setw(6) << icode << endl;
	out << setprecision(9) << matrix;
	if (icode == 1)
	{
		out<< "* row and column names" << endl;
		for (auto &name : row_names)
			out << name << endl;
		
	}
	else
	{
		out << "* row names" << endl;
		for (auto &name : row_names)
			out << name << endl;
		out << "* column names" << endl;
		for (auto &name : col_names)
			out << name << endl;
	}
	out.close();
}

void Mat::from_ascii(const string &filename)
{
	ifstream in(filename);
	if (!in.good()) 
		throw runtime_error("Mat::from_ascii() error: cannot open " + filename + " \
												to read ASCII matrix");
	int nrow = -999, ncol = -999;
	if (in >> nrow >> ncol >> icode){}
	else
		throw runtime_error("Mat::from_ascii() error reading nrow ncol icode from first line\
							 of ASCII matrix file: " + filename);

	vector<Eigen::Triplet<double>> triplet_list;
	double val;
	int irow = 0, jcol = 0;
	for (int inode = 0; inode < nrow*ncol;inode++)
	{
		if (in >> val)
		{
			if (val != 0.0)
				triplet_list.push_back(Eigen::Triplet<double>(irow,jcol,val));	
			jcol++;
			if (jcol >= ncol)
			{
				irow++;
				jcol = 0;
			}
		}
		else
		{
			string i_str = to_string(inode);
			throw runtime_error("Mat::from_ascii() error reading entry number "+i_str+" from\
								 ASCII matrix file: "+filename);
		}
	}
	
	string header;
	//read the newline char
	getline(in, header);
	if (!getline(in,header))
		throw runtime_error("Mat::from_ascii() error reading row/col description\
							 line from ASCII matrix file: " + filename);
	pest_utils::upper_ip(header);
	string name;
	if (icode == 1)
	{
		if (nrow != ncol)
			throw runtime_error("Mat::from_ascii() error: nrow != ncol for icode type 1 ASCII matrix file:" + filename);
		if((header.find("ROW") == string::npos) || (header.find("COLUMN") == string::npos))
			throw runtime_error("Mat::from_ascii() error: expecting row and column names header instead\
								 of:" + header + " in ASCII matrix file: " + filename);
		try
		{
			row_names = read_namelist(in, nrow);
		}
		catch (exception &e)
		{
			throw runtime_error("Mat::from_ascii() error reading row/column names from ASCII matrix file: " + filename + "\n" + e.what());
		}
		if ((nrow != row_names.size()) || (ncol != row_names.size()))
			throw runtime_error("Mat::from_ascii() error: number of row/col names does not match matrix dimensions");
		col_names = row_names;
	}
	else
	{
		if(header.find("ROW") == string::npos)
			throw runtime_error("Mat::from_ascii() error: expecting row names header instead of:" + header + " in ASCII matrix file: " + filename);
		try
		{
			row_names = read_namelist(in, nrow);
		}
		catch (exception &e)
		{
			throw runtime_error("Mat::from_ascii() error reading row names from ASCII matrix file: " + filename + "\n" + e.what());
		}
		if (!getline(in, header))
		{
			throw runtime_error("Mat::from_ascii() error reading column name descriptor from ASCII matrix file: " + filename);
		}
		pest_utils::upper_ip(header);
		if (header.find("COLUMN") == string::npos)
			throw runtime_error("Mat::from_ascii() error: expecting column names header instead of:" + header + " in ASCII matrix file: " + filename);
		try
		{
			col_names = read_namelist(in, ncol);
		}
		catch (exception &e)
		{
			throw runtime_error("Mat::from_ascii() error reading column names from ASCII matrix file: " + filename + "\n" + e.what());
		}
		if (nrow != row_names.size())
			throw runtime_error("Mat::from_ascii() error: nrow != row_names.size() in ASCII matrix file: " + filename);

		if(ncol != col_names.size())
			throw runtime_error("Mat::from_ascii() error: ncol != col_names.size() in ASCII matrix file: " + filename);

	}
	in.close();

	Eigen::SparseMatrix<double> new_matrix(nrow, ncol);
	new_matrix.setZero();  // initialize all entries to 0
	new_matrix.setFromTriplets(triplet_list.begin(), triplet_list.end());
	matrix = new_matrix;
}

vector<string> Mat::read_namelist(ifstream &in, int &nitems)
{
	vector<string> names;
	string name;
	for (int i = 0; i < nitems; i++)
	{
		if (!getline(in, name))
		{
			string i_str = to_string(i);
			throw runtime_error("Mat::read_namelist() error reading name for entry " + i_str);
		}
		if (name.find("*") != string::npos)
		{
			string i_str = to_string(i);
			throw runtime_error("Mat::read_namelist() error: '*' found in item name: " + name+", item number: "+i_str);
		}
		pest_utils::upper_ip(name);
		if (find(names.begin(), names.end(), name) != names.end())
			throw runtime_error("Mat::read_namelist() error: duplicate name: " + name + " found in name list");
		names.push_back(name);
	}
	return names;
}

void Mat::to_binary(const string &filename)
{

}

void Mat::from_binary(const string &filename)
{
	ifstream in;
	in.open(filename.c_str(), ifstream::binary);

	int n_par;
	int n_nonzero;
	int n_obs_and_pi;
	int i, j, n;
	double data;
	char col_name[12];
	char row_name[20];

	// read header
	in.read((char*)&n_par, sizeof(n_par));
	in.read((char*)&n_obs_and_pi, sizeof(n_obs_and_pi));
	if (n_par > 0) throw runtime_error("Mat:::from_binary() error: binary matrix file " + filename + " was produced by deprecated version of PEST");

	n_par = -n_par;
	n_obs_and_pi = -n_obs_and_pi;
	////read number nonzero elements in jacobian (observations + prior information)
	in.read((char*)&n_nonzero, sizeof(n_nonzero));

	// record current position in file
	streampos begin_sen_pos = in.tellg();

	//advance to parameter names section
	in.seekg(n_nonzero*(sizeof(double)+sizeof(int)), ios_base::cur);

	//read parameter names	
	for (int i_rec = 0; i_rec<n_par; ++i_rec)
	{
		in.read(col_name, 12);
		string temp_col = string(col_name, 12);
		pest_utils::strip_ip(temp_col);
		pest_utils::upper_ip(temp_col);
		col_names.push_back(temp_col);
	}
	//read observation and Prior info names
	for (int i_rec = 0; i_rec<n_obs_and_pi; ++i_rec)
	{
		in.read(row_name, 20);
		string temp_row = pest_utils::strip_cp(string(row_name, 20));
		pest_utils::upper_ip(temp_row);
		row_names.push_back(temp_row);
	}

	//return to sensitivity section of file
	in.seekg(begin_sen_pos, ios_base::beg);

	// read matrix
	std::vector<Eigen::Triplet<double> > triplet_list;
	triplet_list.reserve(n_nonzero);
	for (int i_rec = 0; i_rec<n_nonzero; ++i_rec)
	{
		in.read((char*)&(n), sizeof(n));
		--n;
		in.read((char*)&(data), sizeof(data));
		j = int(n / (n_obs_and_pi)); // column index
		i = (n - n_obs_and_pi*j) % n_obs_and_pi;  //row index
		triplet_list.push_back(Eigen::Triplet<double>(i, j, data));
	}
	matrix.resize(n_obs_and_pi, n_par);
	matrix.setZero();
	matrix.setFromTriplets(triplet_list.begin(), triplet_list.end());
	in.close();
}



//-----------------------------------------
//Maninpulate the shape and ordering of Mats
//-----------------------------------------
Mat Mat::get(vector<string> &new_row_names, vector<string> &new_col_names)
{
	//check that every row and col name is listed
	vector<string> row_not_found;
	for (auto &n_row_name : new_row_names)
	{
		if (find(row_names.begin(), row_names.end(), n_row_name) == row_names.end())
			row_not_found.push_back(n_row_name);
	}
	vector<string> col_not_found;
	for (auto &n_col_name : new_col_names)
	{
		if (find(col_names.begin(), col_names.end(), n_col_name) == col_names.end())
			col_not_found.push_back(n_col_name);
	}

	if (row_not_found.size() != 0)
	{
		cout << "Mat::get() error: the following row names were not found:" << endl;
		for (auto &name : row_not_found)
			cout << name << ",";
		cout << endl;
	}

	if (col_not_found.size() != 0)
	{
		cout << "Mat::get() error: the following col names were not found:" << endl;
		for (auto &name : col_not_found)
			cout << name << ",";
		cout << endl;
	}

	if ((row_not_found.size() != 0) || (col_not_found.size() != 0))
	{
		throw runtime_error("Mat::get() error: atleast one row or col name not found in Mat::get()");
	}


	int nrow = new_row_names.size();
	int ncol = new_col_names.size();
	int irow_new;
	int icol_new;

	unordered_map<string, int> row_name2newindex_map;
	unordered_map<string, int> col_name2new_index_map;

	// Build mapping of parameter names to column number in new matrix to be returned
	icol_new = 0;
	for (vector<string>::const_iterator b = new_col_names.begin(), e = new_col_names.end();
		b != e; ++b, ++icol_new) {
		col_name2new_index_map[(*b)] = icol_new;
	}

	// Build mapping of observation names to row  number in new matrix to be returned
	irow_new = 0;
	for (vector<string>::const_iterator b = new_row_names.begin(), e = new_row_names.end();
		b != e; ++b, ++irow_new) {
		row_name2newindex_map[(*b)] = irow_new;
	}
	
	unordered_map<string, int>::const_iterator found_col;
	unordered_map<string, int>::const_iterator found_row;
	unordered_map<string, int>::const_iterator not_found_col_map = col_name2new_index_map.end();
	unordered_map<string, int>::const_iterator not_found_row_map = row_name2newindex_map.end();

	const string *row_name;
	const string *col_name;
	std::vector<Eigen::Triplet<double> > triplet_list;
	for (int icol = 0; icol<matrix.outerSize(); ++icol)
	{
		for (Eigen::SparseMatrix<double>::InnerIterator it(matrix, icol); it; ++it)
		{			
			col_name = &col_names[it.col()];
			row_name = &row_names[it.row()];
			found_col = col_name2new_index_map.find(*col_name);
			found_row = row_name2newindex_map.find(*row_name);

			if (found_col != not_found_col_map && found_row != not_found_row_map)
			{
				triplet_list.push_back(Eigen::Triplet<double>(found_row->second, found_col->second, it.value()));
			}
		}
	}
	if (triplet_list.size() == 0)
		throw runtime_error("Mat::get() error: triplet list is empty");
	Eigen::SparseMatrix<double> new_matrix(nrow, ncol);
	new_matrix.setZero();
	new_matrix.setFromTriplets(triplet_list.begin(), triplet_list.end());
	return Mat(new_row_names,new_col_names,new_matrix,mattype);
}

Mat Mat::extract(vector<string> &extract_row_names, vector<string> &extract_col_names)
{
	Mat new_mat;
	if ((extract_row_names.size() == 0) && (extract_col_names.size() == 0))
		throw runtime_error("Mat::extract() error: extract_rows and extract_cols both empty");	
	else if (extract_row_names.size() == 0)
	{
		new_mat = get(row_names, extract_col_names);
		drop_cols(extract_col_names);
	}
	else if (extract_col_names.size() == 0)
	{
		new_mat = get(extract_row_names, col_names);
		drop_rows(extract_row_names);
	}
	else
	{
		new_mat = get(extract_row_names, extract_col_names);
		drop_rows(extract_row_names);
		drop_cols(extract_col_names);
	}
	return new_mat;
}

void Mat::drop_cols(vector<string> &drop_col_names)
{
	vector<string> missing_col_names;
	for (auto &name : drop_col_names)
		if (find(col_names.begin(), col_names.end(), name) == col_names.end())
			missing_col_names.push_back(name);

	if (missing_col_names.size() != 0)
	{
		cout << "Mat::drop() error: the following drop_col_names were not found:" << endl;
		for (auto &name : drop_col_names)
			cout << name << ',';
		cout << endl;
		throw runtime_error("Mat::drop() error: atleast one drop col name not found");
	}
	vector<string> new_col_names;
	if (drop_col_names.size() == 0) new_col_names = col_names;
	else
	{
		vector<string> new_col_names;
		for (auto &name : col_names)
			if (find(drop_col_names.begin(), drop_col_names.end(), name) == drop_col_names.end())
				new_col_names.push_back(name);
	}
	Mat new_mat = get(row_names, new_col_names);
	matrix = new_mat.get_matrix();
	col_names = new_col_names;
	mattype = new_mat.get_mattype();
}

void Mat::drop_rows(vector<string> &drop_row_names)
{
	
	vector<string> missing_row_names;	
	for (auto &name : drop_row_names)
		if (find(row_names.begin(), row_names.end(), name) == row_names.end())
			missing_row_names.push_back(name);
		
	if (missing_row_names.size() != 0)
	{
		cout << "Mat::drop() error: the following drop_row_names were not found:" << endl;
		for (auto &name : drop_row_names)
			cout << name << ',';
		cout << endl;
		throw runtime_error("Mat::drop() error: atleast one drop row name not found");
	}
	 
	vector<string> new_row_names;
	if (drop_row_names.size() == 0) new_row_names = row_names;
	else
	{
		for (auto &name : row_names)
			if (find(drop_row_names.begin(), drop_row_names.end(), name) == drop_row_names.end())
				new_row_names.push_back(name);
	}
	
	Mat new_mat = get(new_row_names, col_names);
	matrix = new_mat.get_matrix();
	row_names = new_row_names;
	mattype = new_mat.get_mattype();
}



//-----------------------------------------
//covariance matrices
//-----------------------------------------
Covariance::Covariance(string filename)
{
	pest_utils::upper_ip(filename);
	if (filename.find(".PST"))
		throw runtime_error("Cov::Cov() error: cannot instantiate a cov with PST");
	else if (filename.find(".MAT") != string::npos)
		from_ascii(filename);
	else if (filename.find(".UNC") != string::npos)
		from_uncertainty_file(filename);
	else
		throw runtime_error("Cov::Cov() error: only .unc or .mat\
														 files can be used to instatiate a Cov");
}

Covariance::Covariance(vector<string> &names)
{
	row_names = names;
	col_names = names;
	icode = 1;
}

Covariance::Covariance()
{
	icode = 1;
}

Covariance::Covariance(vector<string> _names, Eigen::SparseMatrix<double> _matrix)
{	
	if ((_names.size() != _matrix.rows()) || (_names.size() != _matrix.cols()))
		throw runtime_error("Covariance::Covariance() error: names.size() does not match matrix dimensions");
	matrix = _matrix;
	row_names = _names;
	col_names = _names;
	icode = 1;
}

Covariance::Covariance(Mat _mat)
{
	if (_mat.get_row_names() != _mat.get_col_names())
		throw runtime_error("Cov::Cov() error instantiating Covariance from Mat: row_names != col_names");
	row_names = _mat.get_row_names();
	col_names = _mat.get_col_names();
	matrix = _mat.get_matrix();
	icode = 1;
	mattype = _mat.get_mattype();
}

Covariance Covariance::get(vector<string> &other_names)
{
	Covariance new_cov(Mat::get(other_names, other_names));
	return new_cov;
}

Covariance Covariance::extract(vector<string> &extract_names)
{
	Covariance new_cov(Mat::extract(extract_names, extract_names));
	return new_cov;
}

void Covariance::drop(vector<string> &drop_names)
{
	drop_rows(drop_names);
	drop_cols(drop_names);
}

void Covariance::from_uncertainty_file(const string &filename)
{
	ifstream in(filename);
	if (!in.good())
	{
		throw runtime_error("Cov::from_uncertainty_file() error: cannot open " + filename + " to read uncertainty file: "+filename);
	}
	mattype = MatType::DIAGONAL;
	vector<Eigen::Triplet<double>> triplet_list;
	vector<string> names;
	string line,name;
	double val;
	vector<string> tokens;
	int irow=0, jcol=0;
	
	while (getline(in, line))
	{
		pest_utils::upper_ip(line);
		//if this is the start of some block
		if (line.find("START") != string::npos)
		{
			if (line.find("STANDARD_DEVIATION") != string::npos)
			{
				while (true)
				{
					if (!getline(in, line))
						throw runtime_error("Cov::from_uncertainty_file() error:EOF encountered while reading standard_deviation block\
							from uncertainty file:" + filename);
					pest_utils::upper_ip(line);
					if (line.find("END") != string::npos) break;
					tokens.clear();
					pest_utils::tokenize(line, tokens);
					pest_utils::convert_ip(tokens[1], val);					
					if (find(names.begin(), names.end(), name) != names.end())
						throw runtime_error(name + " listed more than once in uncertainty file:" + filename);
					names.push_back(tokens[0]);
					triplet_list.push_back(Eigen::Triplet<double>(irow, jcol, val));
					irow++, jcol++;
				}

			}
			else if (line.find("COVARIANCE_MATRIX") != string::npos)
			{
				string cov_filename = "none";
				double var_mult = 1.0;
				while (true)
				{
					if (!getline(in, line))
						throw runtime_error("Cov::from_uncertainty_file() error:EOF encountered while reading covariance_matrix block\
							from uncertainty file:" + filename);
					pest_utils::upper_ip(line);
					if (line.find("END") != string::npos) break;

					tokens.clear();
					pest_utils::tokenize(line, tokens);					
					if (tokens[0].find("FILE") != string::npos)
						cov_filename = tokens[1];
					else if (tokens[0].find("VARIANCE") != string::npos)
						pest_utils::convert_ip(tokens[1], var_mult);
					else
						throw runtime_error("Cov::from_uncertainty_file() error:unrecognized token:" + tokens[0] + " in covariance matrix block in uncertainty file:" + filename);
				}
				//read the covariance matrix
				Covariance cov;
				cov.from_ascii(cov_filename);

				//check that the names in the covariance matrix are not already listed
				vector<string> dup_names;
				for (auto &name : cov.get_row_names())
				{
					if (find(names.begin(), names.end(), name) != names.end())
						dup_names.push_back(name);
					else
						names.push_back(name);
				}
				if (dup_names.size() != 0)
				{
					cout << "the following names from covariance matrix file " << cov_filename << " have already be found in uncertainty file " << filename << endl;
					for (auto &name : dup_names)
						cout << name << ',';
					cout << endl;
					throw runtime_error("Cov::from_uncertainty_file() error:atleast one name in covariance matrix " + cov_filename + " is already listed in uncertainty file: " + filename);
				}

				//build triplets from the covariance matrix
				int start_irow = irow;
				Eigen::SparseMatrix<double> cov_matrix = cov.get_matrix();
				for (int icol = 0; icol < cov_matrix.outerSize(); ++icol)
				{
					for (Eigen::SparseMatrix<double>::InnerIterator it(cov_matrix, icol); it; ++it)
					{
						triplet_list.push_back(Eigen::Triplet<double>(irow, jcol, it.value()));
						irow++;
					}
					jcol++;
					irow = start_irow;
				}
				mattype = MatType::BLOCK;
				irow = jcol;
			}
			else
				throw runtime_error("Cov::from_uncertainty_file() error:unrecognized block:" + line + " in uncertainty file:" + filename);
		}
	}

	Eigen::SparseMatrix<double> new_matrix(names.size(), names.size());
	new_matrix.setZero();  // initialize all entries to 0
	new_matrix.setFromTriplets(triplet_list.begin(), triplet_list.end());
	matrix = new_matrix;
	row_names = names;
	col_names = names;
}

void Covariance::from_parameter_bounds(Pest &pest_scenario)
{
	vector<Eigen::Triplet<double>> triplet_list;
	const ParameterRec* par_rec;
	int i = 0;
	double upper, lower;
	for (auto par_name : pest_scenario.get_ctl_ordered_par_names())
	{
		pest_utils::upper_ip(par_name);
		par_rec = pest_scenario.get_ctl_parameter_info().get_parameter_rec_ptr(par_name);
		upper = par_rec->ubnd;
		lower = par_rec->lbnd;
		if (par_rec->tranform_type == ParameterRec::TRAN_TYPE::LOG)
		{
			upper = log10(upper);
			lower = log10(lower);
		}
		if ((par_rec->tranform_type != ParameterRec::TRAN_TYPE::FIXED) && (par_rec->tranform_type != ParameterRec::TRAN_TYPE::TIED))
		{
			row_names.push_back(par_name);
			col_names.push_back(par_name);
			triplet_list.push_back(Eigen::Triplet<double>(i, i, pow((upper - lower) / 4.0, 2.0)));
			i++;
		}
	}
	if (triplet_list.size() > 0)
	{
		matrix.resize(row_names.size(), row_names.size());
		matrix.setFromTriplets(triplet_list.begin(), triplet_list.end());
	}
	else
	{
		throw runtime_error("Cov::from_parameter_bounds() error:Error loading covariance from parameter bounds: no non-fixed/non-tied parameters found");
	}
	mattype = Mat::MatType::DIAGONAL;
}

void Covariance::from_parameter_bounds(const string &pst_filename)
{
	ifstream ipst(pst_filename);
	if (!ipst.good()) throw runtime_error("Cov::from_parameter_bounds() error opening pst file: " + pst_filename);
	Pest pest_scenario;
	pest_scenario.process_ctl_file(ipst, pst_filename);
	from_parameter_bounds(pest_scenario);
}

void Covariance::from_observation_weights(const string &pst_filename)
{
	ifstream ipst(pst_filename);
	if (!ipst.good()) throw runtime_error("Cov::from_observation_weights() error opening pst file: " + pst_filename);
	Pest pest_scenario;
	pest_scenario.process_ctl_file(ipst, pst_filename);
	from_observation_weights(pest_scenario);

}

void Covariance::from_observation_weights(Pest &pest_scenario)
{
	vector<Eigen::Triplet<double>> triplet_list;
	const ObservationRec* obs_rec;
	int i = 0;	
	for (auto obs_name : pest_scenario.get_ctl_ordered_obs_names())
	{
		pest_utils::upper_ip(obs_name);
		obs_rec = pest_scenario.get_ctl_observation_info().get_observation_rec_ptr(obs_name);		
		
		if (obs_rec->weight <= 0.0)
		{
			//cout << "warnging->assigning an artificial weight of 1.0e-30 to observation " << obs_name << endl;
			//triplet_list.push_back(Eigen::Triplet<double>(i, i, 1.0e-30));
		}
		else
		{
			triplet_list.push_back(Eigen::Triplet<double>(i, i, pow(1.0 / obs_rec->weight, 2.0)));
			row_names.push_back(obs_name);
			col_names.push_back(obs_name);
			i++;
		}
	}
	if (row_names.size() > 0)
	{
		matrix.resize(row_names.size(), row_names.size());
		matrix.setFromTriplets(triplet_list.begin(), triplet_list.end());
	}
	else
	{
		throw runtime_error("Cov::from_observation_weights() error:Error loading covariance from obs weights: no non-zero weighted obs found");
	}
	mattype = Mat::MatType::DIAGONAL;


}

void Covariance::to_uncertainty_file(const string &filename)
{
	ofstream out(filename);
	if (!out.good())
	{
		throw runtime_error("Cov::to_uncertainty_file() error opening file: " + filename + " to write an uncertainty file");
	}

	//check if diagonal, write stdevs
	if (mattype == Mat::MatType::DIAGONAL)
	{
		Eigen::VectorXd vec(matrix.diagonal());
		out << "START STANDARD_DEVIATION" << endl;
		int i=0;
		for (vector<string>::iterator name = row_names.begin(); name != row_names.end(); ++name, i++)
		{
			out << "  " << setw(20) << left << *name << "  " << setw(20) << left << vec(i) << endl;
		}
		out << "END STANDARD_DEVIATION" << endl;
		out.close();
	}
	else
	{
		out << "START COVARIANCE_MATRIX" << endl;
		out << "  file emu_cov.mat" << endl;
		out << " variance multiplier 1.0" << endl;
		out << "END COVARIANCE_MATRIX" << endl;
		out.close();
		to_ascii("emu_cov.mat");
	}
}

void Covariance::cholesky()
{	
	Eigen::SimplicialLLT<Eigen::SparseMatrix<double>> llt;
	lower_cholesky = llt.matrixL();
}

vector<Eigen::VectorXd> Covariance::draw(int ndraws)
{
	throw runtime_error("Covariance::draw() not implemented");
}

vector<double> Covariance::standard_normal(default_random_engine gen)
{
	normal_distribution<double> stanard_normal(0.0, 1.0);
	vector<double> sn_vec;
	for (auto &name : row_names)
	{
		sn_vec.push_back(stanard_normal(gen));
	}
	return sn_vec;
}