/* A run's size before it splits for culling; the opaque world sets its own. */
static unsigned s_meshRunTris = 512;

static GevrStaticMesh *buildStaticMesh(const void *gdl, unsigned maxRunTris)
{
	const Gfx *g;
	unsigned ntri = 0, nruns = 0, inRun = 0, runTris = 0, vertexOp = 0;
    size_t stride = sizeof(Vtx);
	if (!gdl)
		return NULL;
	for (g = gdl;; g++) {
		unsigned op = (unsigned)(g->words.w0 >> 24) & 0xff;
		if (op == (G_ENDDL & 0xff))
			break;
		if (op == (G_VTX & 0xff) || op == G_GEVR_WORLD_VTX) {
            if (vertexOp && vertexOp != op) return NULL;
            vertexOp=op;
            stride=op==G_GEVR_WORLD_VTX ? sizeof(GevrWorldVertex) : sizeof(Vtx);
            if (!inRun || runTris >= maxRunTris) { nruns++; runTris = 0; }
			inRun = 1;
		} else if (op == (G_TRI1 & 0xff)) {
			if (!inRun)
				return NULL;   /* drawn from vertices an earlier run loaded */
			ntri++;
			runTris++;
		} else if (op == 0xb1) {
			return NULL;   /* G_TRI4: not a list gevrBeanEmitList makes */
		} else {
			inRun = 0;
		}
	}
	if (!ntri)
		return NULL;
	unsigned cap = 1;
	while (cap < ntri * 6)
		cap <<= 1;
	GevrStaticMesh *m = calloc(1, sizeof(*m));
	uint8_t *verts = malloc((size_t)ntri * 3 * stride);
	uint32_t *indices = malloc((size_t)ntri * 3 * sizeof(uint32_t));
	GevrStaticMeshRun *runs = calloc(nruns, sizeof(*runs));
	uint32_t *table = calloc(cap, sizeof(uint32_t));   /* vertex + 1, 0 empty */
	if (!m || !verts || !indices || !runs || !table) {
		free(m);
		free(verts);
		free(indices);
		free(runs);
		free(table);
		return NULL;
	}
	const uint8_t *slot[16] = {0};
	uint32_t nverts = 0, nindices = 0, run = 0;
	unsigned loaded = 0;
	inRun = 0;
	for (g = gdl;; g++) {
		unsigned op = (unsigned)(g->words.w0 >> 24) & 0xff;
		if (op == vertexOp) {
			unsigned n = (unsigned)(g->words.w0 & 0xffff) / stride, dest = (unsigned)(g->words.w0 >> 16) & 15;
            if (inRun && nindices - runs[run].firstIndex >= maxRunTris * 3) {
                runs[run].end = g;
                runs[run].indexCount = nindices - runs[run].firstIndex;
                run++;
                inRun = 0;
            }
			if (!inRun) {
				runs[run].start = g;
				runs[run].firstIndex = nindices;
				loaded = 0;   /* a run may use only what it loads */
				inRun = 1;
			}
			for (unsigned k = 0; k < n && dest + k < 16; k++) {
				slot[dest + k] = (const uint8_t *)g->words.w1 + k*stride;
				loaded |= 1u << (dest + k);
			}
			continue;
		}
		if (op == (G_TRI1 & 0xff)) {
			const unsigned corner[3] = {(unsigned)(g->words.w1 >> 16 & 255) / 10, (unsigned)(g->words.w1 >> 8 & 255) / 10,
			                           (unsigned)(g->words.w1 & 255) / 10};
			for (int k = 0; k < 3; k++) {
				if (corner[k] >= 16 || !(loaded & (1u << corner[k]))) {
					m->vertices = verts;
					m->indices = indices;
					m->runs = runs;
					free(table);
					gevrStaticMeshFree(m);
					return NULL;
				}
				const uint8_t *v = slot[corner[k]];
				uint32_t h = 2166136261u;
				for (size_t b = 0; b < stride; b++)
					h = (h ^ ((const uint8_t *)v)[b]) * 16777619u;
				for (h &= cap - 1; table[h] && memcmp(verts+(table[h]-1)*stride, v, stride) != 0; h = (h + 1) & (cap - 1))
					;
				if (!table[h]) {
					memcpy(verts+nverts*stride,v,stride);
					table[h] = ++nverts;
				}
				indices[nindices++] = table[h] - 1;
			}
			continue;
		}
		if (inRun) {
			runs[run].end = g;
			runs[run].indexCount = nindices - runs[run].firstIndex;
			run++;
			inRun = 0;
		}
		if (op == (G_ENDDL & 0xff))
			break;
	}
    for (uint32_t r = 0; r < run; r++) {
        const GevrStaticMeshRun *chunk = &runs[r];
        for (uint32_t j = 0; j < chunk->indexCount; j++) {
            const uint8_t *v = verts+indices[chunk->firstIndex+j]*stride;
            const float pos[3] = {
                vertexOp==G_GEVR_WORLD_VTX ? ((const GevrWorldVertex *)v)->p[0] : ((const Vtx *)v)->v.ob[0],
                vertexOp==G_GEVR_WORLD_VTX ? ((const GevrWorldVertex *)v)->p[1] : ((const Vtx *)v)->v.ob[1],
                vertexOp==G_GEVR_WORLD_VTX ? ((const GevrWorldVertex *)v)->p[2] : ((const Vtx *)v)->v.ob[2]};
            for (int k = 0; k < 3; k++) {
                if (!j || pos[k] < runs[r].lo[k]) runs[r].lo[k] = pos[k];
                if (!j || pos[k] > runs[r].hi[k]) runs[r].hi[k] = pos[k];
            }
        }
        runs[r].hasBounds = chunk->indexCount != 0;
    }
	free(table);
	m->vertices = verts;
	m->numVertices = nverts;
    m->worldVertices = vertexOp==G_GEVR_WORLD_VTX;
	m->indices = indices;
	m->numIndices = nindices;
	m->runs = runs;
	m->numRuns = run;
	return m;
}

GevrStaticMesh *gevrStaticMeshBuild(const void *gdl)
{
    return buildStaticMesh(gdl, s_meshRunTris);
}

/* Cluster small features of the same opaque material. Keep original vertex
 * attributes and keep every material. The extra indices share the full-detail
 * vertex buffer so switching detail does not rebuild or upload anything. */
static void buildGearLod(GevrStaticMesh *mesh)
{
    if (mesh->numIndices < 300) return;
    GevrStaticMeshRun *runs = (GevrStaticMeshRun *)mesh->runs;
    const Vtx *vertices = mesh->vertices;
    unsigned largest = 0;
    int min[3], max[3];
    for (int axis = 0; axis < 3; ++axis) min[axis] = max[axis] = vertices[0].v.ob[axis];
    for (uint32_t i = 1; i < mesh->numVertices; ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            const int coordinate = vertices[i].v.ob[axis];
            if (coordinate < min[axis]) min[axis] = coordinate;
            if (coordinate > max[axis]) max[axis] = coordinate;
        }
    }
    for (int axis = 0; axis < 3; ++axis) {
        const unsigned width = (unsigned)(max[axis] - min[axis]);
        if (width > largest) largest = width;
    }
    const int cell = (int)((largest + 43) / 44);
    if (cell < 2) return;

    unsigned cap = 1;
    while (cap < mesh->numVertices * 2) cap <<= 1;
    uint32_t *table = malloc((size_t)cap * sizeof(*table));
    uint32_t *cells = malloc((size_t)mesh->numVertices * sizeof(*cells));
    uint32_t *indices = malloc((size_t)mesh->numIndices * 2 * sizeof(*indices));
    if (!table || !cells || !indices) {
        free(table);
        free(cells);
        free(indices);
        return;
    }
    for (uint32_t i = 0; i < mesh->numVertices; ++i) {
        uint32_t key = 0;
        for (int axis = 0; axis < 3; ++axis)
            key = (key << 6) | (uint32_t)((vertices[i].v.ob[axis] - min[axis]) / cell);
        cells[i] = key;
    }
    memcpy(indices, mesh->indices, (size_t)mesh->numIndices * sizeof(*indices));
    uint32_t end = mesh->numIndices;
    for (uint32_t i = 0; i < mesh->numRuns; ++i) {
        GevrStaticMeshRun *run = &runs[i];
        if (run->indexCount < 96) continue;
        uint32_t j = 0;
        for (; j < run->indexCount; ++j)
            if (vertices[indices[run->firstIndex + j]].v.cn[3] != 255) break;
        if (j != run->indexCount) continue;

        memset(table, 0, (size_t)cap * sizeof(*table));
        uint32_t used = end;
        for (j = 0; j + 2 < run->indexCount; j += 3) {
            uint32_t tri[3];
            for (int k = 0; k < 3; ++k) {
                const uint32_t vertex = indices[run->firstIndex + j + k];
                const uint32_t key = cells[vertex];
                unsigned slot = (key * 2654435761u >> 12) & (cap - 1);
                while (table[slot] && cells[table[slot] - 1] != key)
                    slot = (slot + 1) & (cap - 1);
                if (!table[slot]) table[slot] = vertex + 1;
                tri[k] = table[slot] - 1;
            }
            if (tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0]) continue;
            for (int k = 0; k < 3; ++k) indices[used++] = tri[k];
        }
        if (used > end && used - end < run->indexCount * 3 / 4) {
            run->firstLowIndex = end;
            run->lowIndexCount = used - end;
            end = used;
        }
    }
    free(cells);
    free(table);
    if (end == mesh->numIndices) {
        free(indices);
        return;
    }
    uint32_t *trimmed = realloc(indices, (size_t)end * sizeof(*indices));
    free((void *)mesh->indices);
    mesh->indices = trimmed ? trimmed : indices;
    mesh->numIndicesWithLod = end;
}

GevrStaticMesh *gevrStaticMeshBuildGear(const void *gdl, int makeLod)
{
    /* Gear moves as a unit, so use one draw per contiguous material. */
    GevrStaticMesh *mesh = buildStaticMesh(gdl, UINT32_MAX / 3);
    if (mesh) {
        mesh->tacticalGear = 1;
        if (makeLod && !mesh->worldVertices) buildGearLod(mesh);
    }
    return mesh;
}

void gevrStaticMeshFree(GevrStaticMesh *m)
{
	if (!m)
		return;
	gfx_static_mesh_release(m);
	free((void *)m->vertices);
	free((void *)m->indices);
	free((void *)m->runs);
	free(m);
}

