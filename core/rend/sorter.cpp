/*
	 This file is part of reicast.

	 reicast is free software: you can redistribute it and/or modify
	 it under the terms of the GNU General Public License as published by
	 the Free Software Foundation, either version 2 of the License, or
	 (at your option) any later version.

	 reicast is distributed in the hope that it will be useful,
	 but WITHOUT ANY WARRANTY; without even the implied warranty of
	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	 GNU General Public License for more details.

	 You should have received a copy of the GNU General Public License
	 along with reicast.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "sorter.h"
#include "hw/pvr/Renderer_if.h"
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

struct IndexTrig
{
	u32 id[3];
	u16 pid;
	f32 z;
};

static float minZ(const Vertex *v, const u32 *mod)
{
	return std::min(std::min(v[mod[0]].z, v[mod[1]].z), v[mod[2]].z);
}

static bool operator<(const IndexTrig& left, const IndexTrig& right)
{
	return left.z<right.z;
}

static bool operator<(const PolyParam& left, const PolyParam& right)
{
/* put any condition you want to sort on here */
	return left.zvZ<right.zvZ;
	//return left.zMin<right.zMax;
}

static float getProjectedZ(const Vertex *v, const glm::mat4& mat)
{
	// 1 / w
	return 1 / mat[0][3] * v->x + mat[1][3] * v->y + mat[2][3] * v->z + mat[3][3];
}

void SortPParams(int first, int count)
{
	if (pvrrc.verts.used() == 0 || count <= 1)
		return;

	Vertex* vtx_base=pvrrc.verts.head();
	u32* idx_base = pvrrc.idx.head();

	PolyParam* pp = &pvrrc.global_param_tr.head()[first];
	PolyParam* pp_end = pp + count;

	while(pp!=pp_end)
	{
		if (pp->count<2)
		{
			pp->zvZ=0;
		}
		else
		{
			u32* idx = idx_base + pp->first;

			Vertex* vtx=vtx_base+idx[0];
			Vertex* vtx_end=vtx_base + idx[pp->count-1]+1;

			if (pp->projMatrix != nullptr)
			{
				glm::mat4 mvMat = pp->mvMatrix != nullptr ? glm::make_mat4(pp->mvMatrix) : glm::mat4(1);
				glm::mat4 projMat = glm::make_mat4(pp->projMatrix);
				glm::vec4 min{ 1e38f, 1e38f, 1e38f, 0.f };
				glm::vec4 max{ -1e38f, -1e38f, -1e38f, 0.f };
				while (vtx != vtx_end)
				{
					glm::vec4 pos{ vtx->x, vtx->y, vtx->z, 0.f };
					min = glm::min(min, pos);
					max = glm::max(max, pos);
					vtx++;
				}
				glm::vec4 center = (min + max) / 2.f;
				center.w = 1;
				glm::vec4 extents = max - center;
				// transform
				center = mvMat * center;
				glm::vec3 extentX = mvMat * glm::vec4(extents.x, 0, 0, 0);
				glm::vec3 extentY = mvMat * glm::vec4(0, extents.y, 0, 0);
				glm::vec3 extentZ = mvMat * glm::vec4(0, 0, extents.z, 0);
				// new AA extents
				const float newX = std::abs(glm::dot(glm::vec3{ 1.f, 0.f, 0.f }, extentX)) +
						std::abs(glm::dot(glm::vec3{ 1.f, 0.f, 0.f }, extentY)) +
						std::abs(glm::dot(glm::vec3{ 1.f, 0.f, 0.f }, extentZ));

				const float newY = std::abs(glm::dot(glm::vec3{ 0.f, 1.f, 0.f }, extentX)) +
						std::abs(glm::dot(glm::vec3{ 0.f, 1.f, 0.f }, extentY)) +
						std::abs(glm::dot(glm::vec3{ 0.f, 1.f, 0.f }, extentZ));

				const float newZ = std::abs(glm::dot(glm::vec3{ 0.f, 0.f, 1.f }, extentX)) +
						std::abs(glm::dot(glm::vec3{ 0.f, 0.f, 1.f }, extentY)) +
						std::abs(glm::dot(glm::vec3{ 0.f, 0.f, 1.f }, extentZ));
				min = center - glm::vec4(newX, newY, newZ, 0);
				max = center + glm::vec4(newX, newY, newZ, 0);
				// project
				glm::vec4 a = projMat * min;
				glm::vec4 b = projMat * max;

				pp->zvZ = 1 / std::max(a.w, b.w);
			}
			else
			{
				u32 zv=0xFFFFFFFF;
				while(vtx!=vtx_end)
				{
					zv = std::min(zv, (u32&)vtx->z);
					vtx++;
				}

				pp->zvZ=(f32&)zv;
			}
		}
		pp++;
	}

	std::stable_sort(pvrrc.global_param_tr.head() + first, pvrrc.global_param_tr.head() + first + count);
}

const static Vertex *vtx_sort_base;

#if 0
/*

	Per triangle sorting experiments

*/

//approximate the triangle area
float area_x2(Vertex* v)
{
	return 2/3*fabs( (v[0].x-v[2].x)*(v[1].y-v[0].y) - (v[0].x-v[1].x)*(v[2].y-v[0].y)) ;
}

//approximate the distance ^2
float distance_apprx(Vertex* a, Vertex* b)
{
	float xd=a->x-b->x;
	float yd=a->y-b->y;

	return xd*xd+yd*yd;
}

//was good idea, but not really working ..
bool Intersect(Vertex* a, Vertex* b)
{
	float a1=area_x2(a);
	float a2=area_x2(b);

	float d = distance_apprx(a,b);

	return (a1+a1)>d;
}

//root for quick-union
u16 rid(vector<u16>& v, u16 id)
{
	while(id!=v[id]) id=v[id];
	return id;
}

struct TrigBounds
{
	float xs,xe;
	float ys,ye;
	float zs,ze;
};

//find 3d bounding box for triangle
TrigBounds bound(Vertex* v)
{
	TrigBounds rv = {	std::min(std::min(v[0].x, v[1].x), v[2].x), std::max(std::max(v[0].x,v[1].x),v[2].x),
						std::min(std::min(v[0].y, v[1].y), v[2].y), std::max(std::max(v[0].y,v[1].y),v[2].y),
						std::min(std::min(v[0].z, v[1].z), v[2].z), std::max(std::max(v[0].z,v[1].z),v[2].z),
					};

	return rv;
}

//bounding box 2d intersection
bool Intersect(TrigBounds& a, TrigBounds& b)
{
	return  ( !(a.xe<b.xs || a.xs>b.xe) && !(a.ye<b.ys || a.ys>b.ye) /*&& !(a.ze<b.zs || a.zs>b.ze)*/ );
}


bool operator<(const IndexTrig &left, const IndexTrig &right)
{
	/*
	TrigBounds l=bound(vtx_sort_base+left.id);
	TrigBounds r=bound(vtx_sort_base+right.id);

	if (!Intersect(l,r))
	{
		return true;
	}
	else
	{
		return (l.zs + l.ze) < (r.zs + r.ze);
	}*/

	return minZ(&vtx_sort_base[left.id])<minZ(&vtx_sort_base[right.id]);
}

//Not really working cuz of broken intersect
bool Intersect(const IndexTrig &left, const IndexTrig &right)
{
	TrigBounds l=bound(vtx_sort_base+left.id);
	TrigBounds r=bound(vtx_sort_base+right.id);

	return Intersect(l,r);
}

#endif

//are two poly params the same?
static bool PP_EQ(const PolyParam *pp0, const PolyParam *pp1)
{
	return (pp0->pcw.full & PCW_DRAW_MASK) == (pp1->pcw.full & PCW_DRAW_MASK) && pp0->isp.full == pp1->isp.full
			&& pp0->tcw.full == pp1->tcw.full && pp0->tsp.full == pp1->tsp.full && pp0->tileclip == pp1->tileclip
			&& pp0->mvMatrix == pp1->mvMatrix && pp0->projMatrix == pp1->projMatrix
			&& pp0->lightModel == pp1->lightModel && pp0->envMapping == pp1->envMapping;
}

static void fill_id(u32 *d, const Vertex *v0, const Vertex *v1, const Vertex *v2,  const Vertex *vb)
{	
	d[0] = (u32)(v0 - vb);
	d[1] = (u32)(v1 - vb);
	d[2] = (u32)(v2 - vb);
}

void GenSorted(int first, int count, std::vector<SortTrigDrawParam>& pidx_sort, std::vector<u32>& vidx_sort)
{
	u32 tess_gen=0;

	pidx_sort.clear();

	if (pvrrc.verts.used() == 0 || count == 0)
		return;

	const Vertex * const vtx_base = pvrrc.verts.head();
	const u32 * const idx_base = pvrrc.idx.head();

	const PolyParam * const pp_base = &pvrrc.global_param_tr.head()[first];
	const PolyParam *pp = pp_base;
	const PolyParam * const pp_end = pp + count;
	while (pp->count == 0 && pp < pp_end)
		pp++;
	if (pp == pp_end)
		return;

	vtx_sort_base=vtx_base;

	static u32 vtx_cnt;

	int vtx_count = pvrrc.verts.used() - idx_base[pp->first];
	if ((u32)vtx_count > vtx_cnt)
		vtx_cnt = vtx_count;

#if PRINT_SORT_STATS
	printf("TVTX: %d || %d\n",vtx_cnt,vtx_count);
#endif

	if (vtx_count<=0)
		return;

	//make lists of all triangles, with their pid and vid
	static std::vector<IndexTrig> lst;

	lst.resize(vtx_count*4);


	int pfsti=0;

	while (pp != pp_end)
	{
		u32 ppid = (u32)(pp - pp_base);

		if (pp->count > 2)
		{
			const u32 *idx = idx_base + pp->first;
			u32 flip = 0;
			glm::mat4 mat;
			float z0, z1;

			if (pp->projMatrix != nullptr)
			{
				mat = glm::make_mat4(pp->projMatrix);
				if (pp->mvMatrix != nullptr)
					mat *= glm::make_mat4(pp->mvMatrix);
				z0 = getProjectedZ(vtx_base + idx[0], mat);
				z1 = getProjectedZ(vtx_base + idx[1], mat);
			}
			for (u32 i = 0; i < pp->count - 2; i++)
			{
				const Vertex *v0, *v1;
				if (flip)
				{
					v0 = vtx_base + idx[i + 1];
					v1 = vtx_base + idx[i];
				}
				else
				{
					v0 = vtx_base + idx[i];
					v1 = vtx_base + idx[i + 1];
				}
				const Vertex *v2 = vtx_base + idx[i + 2];
				fill_id(lst[pfsti].id, v0, v1, v2, vtx_base);
				lst[pfsti].pid = ppid;
				if (pp->projMatrix != nullptr)
				{
					float z2 = getProjectedZ(v2, mat);
					lst[pfsti].z = std::min(z0, std::min(z1, z2));
					z0 = z1;
					z1 = z2;
				}
				else
				{
					lst[pfsti].z = minZ(vtx_base, lst[pfsti].id);
				}
				pfsti++;

				flip ^= 1;
			}
		}
		pp++;
	}

	u32 aused=pfsti;

	lst.resize(aused);

	//sort them
#if 1
	std::stable_sort(lst.begin(),lst.end());

	//Merge pids/draw cmds if two different pids are actually equal
	if (true)
	{
		for (u32 k=1;k<aused;k++)
		{
			if (lst[k].pid!=lst[k-1].pid)
			{
				if (PP_EQ(&pp_base[lst[k].pid],&pp_base[lst[k-1].pid]))
				{
					lst[k].pid=lst[k-1].pid;
				}
			}
		}
	}
#endif


#if 0
	//tries to optimise draw calls by reordering non-intersecting polygons
	//uber slow and not very effective
	{
		int opid=lst[0].pid;

		for (int k=1;k<aused;k++)
		{
			if (lst[k].pid!=opid)
			{
				if (opid>lst[k].pid)
				{
					//MOVE UP
					for (int j=k;j>0 && lst[j].pid!=lst[j-1].pid && !Intersect(lst[j],lst[j-1]);j--)
					{
						swap(lst[j],lst[j-1]);
					}
				}
				else
				{
					//move down
					for (int j=k+1;j<aused && lst[j].pid!=lst[j-1].pid && !Intersect(lst[j],lst[j-1]);j++)
					{
						swap(lst[j],lst[j-1]);
					}
				}
			}

			opid=lst[k].pid;
		}
	}
#endif

	//re-assemble them into drawing commands
	vidx_sort.resize(aused*3);

	int idx=-1;

	for (u32 i=0; i<aused; i++)
	{
		int pid=lst[i].pid;
		u32* midx = lst[i].id;

		vidx_sort[i*3 + 0]=midx[0];
		vidx_sort[i*3 + 1]=midx[1];
		vidx_sort[i*3 + 2]=midx[2];

		if (idx!=pid /* && !PP_EQ(&pp_base[pid],&pp_base[idx]) */ )
		{
			SortTrigDrawParam stdp = { pp_base + pid, i * 3, 0 };

			if (idx!=-1)
			{
				SortTrigDrawParam& last = pidx_sort.back();
				last.count = stdp.first - last.first;
			}

			pidx_sort.push_back(stdp);
			idx=pid;
		}
	}

	if (!pidx_sort.empty())
	{
		SortTrigDrawParam& last = pidx_sort.back();
		last.count = aused * 3 - last.first;
	}

#if PRINT_SORT_STATS
	printf("Reassembled into %d from %d\n",pidx_sort.size(),pp_end-pp_base);
#endif

	if (tess_gen) DEBUG_LOG(RENDERER, "Generated %.2fK Triangles !", tess_gen / 1000.0);
}

// Vulkan and DirectX use the color values of the first vertex for flat shaded triangle strips.
// On Dreamcast the last vertex is the provoking one so we must copy it onto the first.
void setFirstProvokingVertex(rend_context& rendContext)
{
	auto setProvokingVertex = [&rendContext](const List<PolyParam>& list) {
        u32 *idx_base = rendContext.idx.head();
        Vertex *vtx_base = rendContext.verts.head();
		for (const PolyParam& pp : list)
		{
			if (pp.pcw.Gouraud)
				continue;
			for (u32 i = 0; i + 2 < pp.count; i++)
			{
				Vertex& vertex = vtx_base[idx_base[pp.first + i]];
				Vertex& lastVertex = vtx_base[idx_base[pp.first + i + 2]];
				memcpy(vertex.col, lastVertex.col, sizeof(vertex.col));
				memcpy(vertex.spc, lastVertex.spc, sizeof(vertex.spc));
				memcpy(vertex.col1, lastVertex.col1, sizeof(vertex.col1));
				memcpy(vertex.spc1, lastVertex.spc1, sizeof(vertex.spc1));
			}
		}
	};
	setProvokingVertex(rendContext.global_param_op);
	setProvokingVertex(rendContext.global_param_pt);
	setProvokingVertex(rendContext.global_param_tr);
}
