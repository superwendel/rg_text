// Frozen queue implementation from commit cf3c982, before the packing experiment.
// Deliberately preserve its per-quad field stores and counter updates for comparison.
// Include rg_text_gpu.h first. This reference is benchmark-only.
#ifndef RG_TEXT_BENCH_QUAD_PACK_REFERENCE_H
#define RG_TEXT_BENCH_QUAD_PACK_REFERENCE_H
RGINLINE size_t rg_text_bench_queue_reference(RgTextGpuRenderer* renderer,
                                        const RgTextQuad* quads,
                                        size_t quad_count)
{
	if (!renderer || !quads || quad_count == 0u)
	{
		return 0u;
	}

	size_t available = (size_t)renderer->quad_capacity - (size_t)renderer->quad_count;
	if (quad_count > available)
	{
		quad_count = available;
	}

	for (size_t i = 0u; i < quad_count; i++)
	{
		const RgTextQuad* q = &quads[i];
		u32 vertex_base = renderer->vertex_count;
		u32 index_base = renderer->index_count;
		RgTextGpuVertex* v = &renderer->vertices[vertex_base];
		u32* idx = &renderer->indices[index_base];

		v[0].x = q->x0;
		v[0].y = q->y0;
		v[0].z = 0.0f;
		v[0].r = q->color.r;
		v[0].g = q->color.g;
		v[0].b = q->color.b;
		v[0].a = q->color.a;
		v[0].u = q->u0;
		v[0].v = q->v0;
		v[1].x = q->x1;
		v[1].y = q->y0;
		v[1].z = 0.0f;
		v[1].r = q->color.r;
		v[1].g = q->color.g;
		v[1].b = q->color.b;
		v[1].a = q->color.a;
		v[1].u = q->u1;
		v[1].v = q->v0;
		v[2].x = q->x1;
		v[2].y = q->y1;
		v[2].z = 0.0f;
		v[2].r = q->color.r;
		v[2].g = q->color.g;
		v[2].b = q->color.b;
		v[2].a = q->color.a;
		v[2].u = q->u1;
		v[2].v = q->v1;
		v[3].x = q->x0;
		v[3].y = q->y1;
		v[3].z = 0.0f;
		v[3].r = q->color.r;
		v[3].g = q->color.g;
		v[3].b = q->color.b;
		v[3].a = q->color.a;
		v[3].u = q->u0;
		v[3].v = q->v1;

		idx[0] = vertex_base + 0u;
		idx[1] = vertex_base + 1u;
		idx[2] = vertex_base + 2u;
		idx[3] = vertex_base + 0u;
		idx[4] = vertex_base + 2u;
		idx[5] = vertex_base + 3u;

		renderer->quad_count++;
		renderer->vertex_count += 4u;
		renderer->index_count += 6u;
	}

	return quad_count;
}


#endif

