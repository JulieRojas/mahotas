#include <algorithm>
#include <queue>
#include <cstdio>

#include "array.hpp"
#include "dispatch.hpp"

extern "C" {
    #include <Python.h>
    #include <numpy/ndarrayobject.h>
}

namespace{

const char TypeErrorMsg[] = 
    "Type not understood. "
    "This is caused by either a direct call to _morph (which is dangerous: types are not checked!) or a bug in morph.py.\n";


template <typename T>
numpy::position central_position(const numpy::array_base<T>& array) {
    numpy::position res(array.raw_dims(), array.ndims());
    for (int i = 0, nd = array.ndims(); i != nd; ++i) res.position_[i] /= 2;
    return res;
}

template<typename T>
void erode(numpy::aligned_array<T> res, numpy::array<T> array, numpy::aligned_array<T> Bc) {
    const unsigned N = res.size();
    const unsigned N2 = Bc.size();
    const numpy::position centre = central_position(Bc);
    typename numpy::aligned_array<T>::iterator rpos = res.begin();

    for (int i = 0; i != N; ++i, ++rpos) {
        bool on = true;
        typename numpy::aligned_array<T>::iterator startc = Bc.begin();
        for (int j = 0; j != N2; ++j, ++startc) {
            if (*startc) {
                numpy::position npos = rpos.position() + startc.position() - centre;
                if (array.validposition(npos) && !array.at(npos)) {
                    on = false;
                    break;
                }
            }
        }
        *rpos = on;
    }
}


PyObject* py_erode(PyObject* self, PyObject* args, PyObject* kwds) {
    PyArrayObject* array;
    PyArrayObject* Bc;
    static char * kwlist[] = { "array", "Bc", NULL };
    if (!PyArg_ParseTupleAndKeywords(args,kwds,"OO",kwlist,
                    &array,
                    &Bc)) {
        return NULL;
    }
    PyArrayObject* res_a = (PyArrayObject*)PyArray_FromDims(array->nd,array->dimensions,PyArray_TYPE(array));
    if (!res_a) { 
        return NULL;
    }
    switch(PyArray_TYPE(array)) {
#define HANDLE(type) \
    erode<type>(numpy::aligned_array<type>(res_a),numpy::array<type>(array),numpy::aligned_array<type>(Bc));\

        HANDLE_INTEGER_TYPES();
#undef HANDLE
        default:
        PyErr_SetString(PyExc_RuntimeError,TypeErrorMsg);
        return NULL;
    }
    return PyArray_Return(res_a);
}

template<typename T>
void dilate(numpy::aligned_array<T> res, numpy::array<T> array, numpy::aligned_array<T> Bc) {
    const unsigned N = res.size();
    const unsigned N2 = Bc.size();
    const numpy::position centre = central_position(Bc);

    typename numpy::array<T>::iterator pos = array.begin();
    for (int i = 0; i != N; ++i, ++pos) {
        if (*pos) {
            typename numpy::aligned_array<T>::iterator startc = Bc.begin();
            for (int j = 0; j != N2; ++j, ++startc) {
                if (*startc) {
                    numpy::position npos = pos.position() + startc.position() - centre;
                    if (res.validposition(npos)) {
                        res.at(npos) = *pos+*startc;
                    }
                }
            }
        }
    }

}

PyObject* py_dilate(PyObject* self, PyObject* args, PyObject* kwds) {
    PyArrayObject* array;
    PyArrayObject* Bc;
    static char * kwlist[] = { "array", "Bc", NULL };
    if (!PyArg_ParseTupleAndKeywords(args,kwds,"OO",kwlist,
                    &array,
                    &Bc)) {
        return NULL;
    }
    PyArrayObject* res_a = (PyArrayObject*)PyArray_FromDims(array->nd,array->dimensions,PyArray_TYPE(array));
    if (!res_a) { 
        return NULL;
    }
    switch(PyArray_TYPE(array)) {
#define HANDLE(type) \
    dilate<type>(numpy::aligned_array<type>(res_a),numpy::array<type>(array),numpy::aligned_array<type>(Bc));\

        HANDLE_INTEGER_TYPES();
#undef HANDLE
        default:
        PyErr_SetString(PyExc_RuntimeError,TypeErrorMsg);
        return NULL;
    }
    return PyArray_Return(res_a);
}

struct MarkerInfo { 
    int cost;
    int idx;
    numpy::position pos;
    MarkerInfo(int cost, int idx, numpy::position pos)
        :cost(cost),
        idx(idx),
        pos(pos) {
        }
    bool operator < (const MarkerInfo& other) const {
        // We want the smaller argument to compare higher, so we reverse the order here:
        if (cost == other.cost) return idx > other.idx;
        return cost > other.cost;
    }
};

template<typename BaseType>
void cwatershed(numpy::aligned_array<BaseType> res, numpy::array<BaseType> array, numpy::array<BaseType> markers, numpy::aligned_array<BaseType> Bc) {
    const unsigned N = res.size();
    const unsigned N2 = Bc.size();
    const numpy::position centre = central_position(Bc);
    int idx = 0;

    numpy::aligned_array<BaseType> cost = array_like(array);
    std::fill_n(cost.data(),cost.size(),std::numeric_limits<BaseType>::max());
    numpy::aligned_array<bool> status((PyArrayObject*)PyArray_FromDims(array.ndims(),const_cast<npy_intp*>(array.raw_dims()),NPY_BOOL));
    std::fill_n(status.data(),status.size(),false);
    std::priority_queue<MarkerInfo> hqueue;
     
    typename numpy::array<BaseType>::iterator mpos = markers.begin();
    for (int i =0; i != N; ++i, ++mpos) {
        if (*mpos) {
            assert(markers.validposition(mpos.position()));
            hqueue.push(MarkerInfo(*mpos,idx++,mpos.position()));
            res.at(mpos.position()) = *mpos;
            cost.at(mpos.position()) = array.at(mpos.position());
        }
    }

    while (!hqueue.empty()) {
        const numpy::position pos = hqueue.top().pos;
        hqueue.pop();
        status.at(pos) = true;
        typename numpy::aligned_array<BaseType>::iterator Bi = Bc.begin();
        for (int j = 0; j != N2; ++j, ++Bi) {
            if (*Bi) {
                numpy::position npos = pos + Bi.position() - centre;
                if (status.validposition(npos) && !status.at(npos)) {
                    BaseType ncost = array.at(npos);
                    if (ncost < cost.at(npos)) {
                        cost.at(npos) = ncost;
                        res.at(npos) = res.at(pos);
                        hqueue.push(MarkerInfo(ncost,idx++,npos));
                    }
                }
            }
        }
    }
}

PyObject* py_cwatershed(PyObject* self, PyObject* args, PyObject* kwds) {
    PyArrayObject* array;
    PyArrayObject* markers;
    PyArrayObject* Bc;
    static char * kwlist[] = { "array", "markers", "Bc", NULL };
    if (!PyArg_ParseTupleAndKeywords(args,kwds,"OOO",kwlist,
                    &array,
                    &markers,
                    &Bc)) {
        return NULL;
    }
    PyArrayObject* res_a = (PyArrayObject*)PyArray_FromDims(array->nd,array->dimensions,PyArray_TYPE(array));
    if (!res_a) { 
        return NULL;
    }
    switch(PyArray_TYPE(array)) {
#define HANDLE(type) \
    cwatershed<type>(numpy::aligned_array<type>(res_a),numpy::array<type>(array),numpy::array<type>(markers),numpy::aligned_array<type>(Bc));\

        HANDLE_INTEGER_TYPES();
#undef HANDLE
        default:
        PyErr_SetString(PyExc_RuntimeError,TypeErrorMsg);
        return NULL;
    }
    return PyArray_Return(res_a);
}


PyMethodDef methods[] = {
  {"dilate",(PyCFunction)py_dilate, (METH_VARARGS|METH_KEYWORDS), NULL},
  {"erode",(PyCFunction)py_erode, (METH_VARARGS|METH_KEYWORDS), NULL},
  {"cwatershed",(PyCFunction)py_cwatershed, (METH_VARARGS|METH_KEYWORDS), NULL},
  {NULL, NULL,0,NULL},
};

} // namespace
extern "C"
void init_morph()
  {
    import_array();
    (void)Py_InitModule("_morph", methods);
  }

