#pragma once
// OCC kopru C-API'si: farkli derleyici/toolchain'ler arasinda güvenli tek
// arayuz. Yalniz POD tipler + malloc/free — Qt uygulamasi (mingw1310) ile
// mm_occ.dll (MSYS2 GCC) arasinda ABI riski yoktur.
#ifdef __cplusplus
extern "C" {
#endif

const char* mm_occ_version(void);

// Donus 0 = basarili. Cikti tamponlari malloc ile ayrilir; cagiran
// mm_occ_free ile serbest birakir. Vertex'ler float ucuzler (x,y,z),
// kenarlar unsigned int ciftler (from, to).
int mm_occ_solid_box(double dx, double dy, double dz,
                     float** out_vertices, int* out_vertex_count,
                     unsigned int** out_edges, int* out_edge_count);

int mm_occ_solid_cylinder(double radius, double height, int segments,
                          float** out_vertices, int* out_vertex_count,
                          unsigned int** out_edges, int* out_edge_count);

void mm_occ_free(void* ptr);

#ifdef __cplusplus
}
#endif
