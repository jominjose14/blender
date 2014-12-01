/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/mesh_sample.c
 *  \ingroup bke
 *
 * Sample a mesh surface or volume and evaluate samples on deformed meshes.
 */

#include "MEM_guardedalloc.h"

#include "DNA_key_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "BLI_utildefines.h"
#include "BLI_math.h"
#include "BLI_rand.h"

#include "BKE_bvhutils.h"
#include "BKE_mesh_sample.h"
#include "BKE_customdata.h"
#include "BKE_DerivedMesh.h"

#include "BLI_strict_flags.h"

/* ==== Evaluate ==== */

bool BKE_mesh_sample_eval(DerivedMesh *dm, const MSurfaceSample *sample, float loc[3], float nor[3])
{
	MVert *mverts = dm->getVertArray(dm);
	unsigned int totverts = (unsigned int)dm->getNumVerts(dm);
	MVert *v1, *v2, *v3;
	float vnor[3];
	
	zero_v3(loc);
	zero_v3(nor);
	
	if (sample->orig_verts[0] >= totverts ||
	    sample->orig_verts[1] >= totverts ||
	    sample->orig_verts[2] >= totverts)
		return false;
	
	v1 = &mverts[sample->orig_verts[0]];
	v2 = &mverts[sample->orig_verts[1]];
	v3 = &mverts[sample->orig_verts[2]];
	
	madd_v3_v3fl(loc, v1->co, sample->orig_weights[0]);
	madd_v3_v3fl(loc, v2->co, sample->orig_weights[1]);
	madd_v3_v3fl(loc, v3->co, sample->orig_weights[2]);
	
	normal_short_to_float_v3(vnor, v1->no);
	madd_v3_v3fl(nor, vnor, sample->orig_weights[0]);
	normal_short_to_float_v3(vnor, v2->no);
	madd_v3_v3fl(nor, vnor, sample->orig_weights[1]);
	normal_short_to_float_v3(vnor, v3->no);
	madd_v3_v3fl(nor, vnor, sample->orig_weights[2]);
	
	normalize_v3(nor);
	
	return true;
}

bool BKE_mesh_sample_shapekey(Key *key, KeyBlock *kb, const MSurfaceSample *sample, float loc[3])
{
	float *v1, *v2, *v3;
	
	BLI_assert(key->elemsize == 3 * sizeof(float));
	BLI_assert(sample->orig_verts[0] < (unsigned int)kb->totelem);
	BLI_assert(sample->orig_verts[1] < (unsigned int)kb->totelem);
	BLI_assert(sample->orig_verts[2] < (unsigned int)kb->totelem);
	
	v1 = (float *)kb->data + sample->orig_verts[0] * 3;
	v2 = (float *)kb->data + sample->orig_verts[1] * 3;
	v3 = (float *)kb->data + sample->orig_verts[2] * 3;
	
	madd_v3_v3fl(loc, v1, sample->orig_weights[0]);
	madd_v3_v3fl(loc, v2, sample->orig_weights[1]);
	madd_v3_v3fl(loc, v3, sample->orig_weights[2]);
	
	/* TODO use optional vgroup weights to determine if a shapeky actually affects the sample */
	return true;
}


/* ==== Sampling Utilities ==== */

BLI_INLINE void mesh_sample_weights_from_loc(MSurfaceSample *sample, DerivedMesh *dm, int face_index, const float loc[3])
{
	MFace *face = &dm->getTessFaceArray(dm)[face_index];
	unsigned int index[4] = { face->v1, face->v2, face->v3, face->v4 };
	MVert *mverts = dm->getVertArray(dm);
	
	float *v1 = mverts[face->v1].co;
	float *v2 = mverts[face->v2].co;
	float *v3 = mverts[face->v3].co;
	float *v4 = face->v4 ? mverts[face->v4].co : NULL;
	float w[4];
	int tri[3];
	
	interp_weights_face_v3_index(tri, w, v1, v2, v3, v4, loc);
	
	sample->orig_verts[0] = index[tri[0]];
	sample->orig_verts[1] = index[tri[1]];
	sample->orig_verts[2] = index[tri[2]];
	sample->orig_weights[0] = w[tri[0]];
	sample->orig_weights[1] = w[tri[1]];
	sample->orig_weights[2] = w[tri[2]];
}

/* ==== Sampling ==== */

static bool mesh_sample_store_array_sample(void *vdata, int capacity, int index, const MSurfaceSample *sample)
{
	MSurfaceSample *data = vdata;
	if (index >= capacity)
		return false;
	
	data[index] = *sample;
	return true;
}

void BKE_mesh_sample_storage_single(MSurfaceSampleStorage *storage, MSurfaceSample *sample)
{
	/* handled as just a special array case with capacity = 1 */
	storage->store_sample = mesh_sample_store_array_sample;
	storage->capacity = 1;
	storage->data = sample;
	storage->free_data = false;
}

void BKE_mesh_sample_storage_array(MSurfaceSampleStorage *storage, MSurfaceSample *samples, int capacity)
{
	storage->store_sample = mesh_sample_store_array_sample;
	storage->capacity = capacity;
	storage->data = samples;
	storage->free_data = false;
}

void BKE_mesh_sample_storage_release(MSurfaceSampleStorage *storage)
{
	if (storage->free_data)
		MEM_freeN(storage->data);
}


int BKE_mesh_sample_generate_random(MSurfaceSampleStorage *dst, DerivedMesh *dm, unsigned int seed, int totsample)
{
	MFace *mfaces;
	int totfaces;
	RNG *rng;
	MFace *mface;
	float a, b;
	int i, stored = 0;
	
	rng = BLI_rng_new(seed);
	
	DM_ensure_tessface(dm);
	mfaces = dm->getTessFaceArray(dm);
	totfaces = dm->getNumTessFaces(dm);
	
	for (i = 0; i < totsample; ++i) {
		MSurfaceSample sample = {{0}};
		
		mface = &mfaces[BLI_rng_get_int(rng) % totfaces];
		
		if (mface->v4 && BLI_rng_get_int(rng) % 2 == 0) {
			sample.orig_verts[0] = mface->v3;
			sample.orig_verts[1] = mface->v4;
			sample.orig_verts[2] = mface->v1;
		}
		else {
			sample.orig_verts[0] = mface->v1;
			sample.orig_verts[1] = mface->v2;
			sample.orig_verts[2] = mface->v3;
		}
		
		a = BLI_rng_get_float(rng);
		b = BLI_rng_get_float(rng);
		if (a + b > 1.0f) {
			a = 1.0f - a;
			b = 1.0f - b;
		}
		sample.orig_weights[0] = 1.0f - (a + b);
		sample.orig_weights[1] = a;
		sample.orig_weights[2] = b;
		
		if (dst->store_sample(dst->data, dst->capacity, i, &sample))
			++stored;
		else
			break;
	}
	
	BLI_rng_free(rng);
	
	return stored;
}


static bool sample_bvh_raycast(MSurfaceSample *sample, DerivedMesh *dm, BVHTreeFromMesh *bvhdata, const float ray_start[3], const float ray_end[3])
{
	BVHTreeRayHit hit;
	float ray_normal[3], dist;

	sub_v3_v3v3(ray_normal, ray_end, ray_start);
	dist = normalize_v3(ray_normal);
	
	hit.index = -1;
	hit.dist = dist;

	if (BLI_bvhtree_ray_cast(bvhdata->tree, ray_start, ray_normal, 0.0f,
	                         &hit, bvhdata->raycast_callback, bvhdata) >= 0) {
		
		mesh_sample_weights_from_loc(sample, dm, hit.index, hit.co);
		
		return true;
	}
	else
		return false;
}

int BKE_mesh_sample_generate_raycast(MSurfaceSampleStorage *dst, DerivedMesh *dm, MeshSampleRayCallback ray_cb, void *userdata, int totsample)
{
	BVHTreeFromMesh bvhdata;
	float ray_start[3], ray_end[3];
	int i, stored = 0;
	
	DM_ensure_tessface(dm);
	
	memset(&bvhdata, 0, sizeof(BVHTreeFromMesh));
	bvhtree_from_mesh_faces(&bvhdata, dm, 0.0f, 4, 6);
	
	if (bvhdata.tree) {
		for (i = 0; i < totsample; ++i) {
			if (ray_cb(userdata, ray_start, ray_end)) {
				MSurfaceSample sample;
				if (sample_bvh_raycast(&sample, dm, &bvhdata, ray_start, ray_end)) {
					if (dst->store_sample(dst->data, dst->capacity, i, &sample))
						++stored;
					else
						break;
				}
			}
		}
	}
	
	free_bvhtree_from_mesh(&bvhdata);
	
	return stored;
}
