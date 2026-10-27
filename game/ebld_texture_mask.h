#ifndef EBLD_TEXTURE_MASK_H
#define EBLD_TEXTURE_MASK_H

static uint64_t ebld_all_texture_mask(const EbldModel& model)
{
    return model.tex.size() >= 64 ? ~uint64_t(0)
         : (uint64_t(1) << model.tex.size()) - 1;
}

static uint64_t ebld_texture_bit(const EbldModel& model, int texture)
{
    return texture >= 0 && texture < 64 && (size_t)texture < model.tex.size()
         ? uint64_t(1) << texture : 0;
}

static void ebld_derive_texture_masks(EbldModel& model)
{
    for (int stage = EB_INTACT; stage <= EB_WRECKED; stage++)
        model.stageTextures[stage] = model.nset < 2 ? ebld_all_texture_mask(model) : 0;
    if (model.nset < 2) return;
    for (size_t index = 0; index < model.set[EBS_INTACT].tri.size(); index++)
        model.stageTextures[EB_INTACT] |= ebld_texture_bit(model, model.set[EBS_INTACT].tri[index].tex);
    for (size_t index = 0; index < model.set[EBS_CUT].tri.size(); index++) {
        const EbldTri& triangle = model.set[EBS_CUT].tri[index];
        model.stageTextures[EB_HELD] |= ebld_texture_bit(model, triangle.tex);
        if (triangle.kind == 0 && triangle.texOver >= 0 && triangle.mode != MODE_XLU)
            model.stageTextures[EB_HELD] |= ebld_texture_bit(model, triangle.texOver);
        model.stageTextures[EB_DAMAGED] |= ebld_texture_bit(model, triangle.texDmg)
                                         | ebld_texture_bit(model, triangle.texGlow);
    }
    if (model.nset == EBS_N) {
        for (size_t index = 0; index < model.set[EBS_DESTROYED].tri.size(); index++) {
            const EbldTri& triangle = model.set[EBS_DESTROYED].tri[index];
            model.stageTextures[EB_WRECKED] |= ebld_texture_bit(model, triangle.tex)
                                             | ebld_texture_bit(model, triangle.texGlow);
        }
    } else {
        model.stageTextures[EB_WRECKED] = model.stageTextures[EB_DAMAGED];
    }
    if (model.param.size() > EBP_SCORCH_TEX && model.param[EBP_SCORCH_TEX] >= 0.0f)
        model.stageTextures[EB_WRECKED] |= ebld_texture_bit(model, (int)model.param[EBP_SCORCH_TEX]);
}

static bool ebld_stage_residency_enabled(const EbldModel& model)
{
    return !g_raModule && model.texOwner < 0 && model.nset >= 2 && model.tex.size() <= 64
        && mesh_perf_switch("OPENCNC3D_EBLD_STAGE_RESIDENCY");
}

static uint64_t ebld_texture_mask_for_stage(const EbldModel& model, int stage)
{
    if (!ebld_stage_residency_enabled(model) || stage < EB_INTACT || stage > EB_WRECKED)
        return ebld_all_texture_mask(model);
    return model.stageTextures[stage];
}

#endif
