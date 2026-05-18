//{
// HDR 环境贴图重要性采样    
//Ray hdrTestRay;
//hdrTestRay.startPoint = hit.hitPoint;
//hdrTestRay.direction = SampleHdr(rand(), rand());

/*
// 进行一次求交测试 判断是否有遮挡
if(dot(N, hdrTestRay.direction) > 0.0) { // 如果采样方向背向点 p 则放弃测试, 因为 N dot L < 0            
    HitResult hdrHit = hitBVH(hdrTestRay);
    
    // 天空光仅在没有遮挡的情况下积累亮度
    if(!hdrHit.isHit) {
        vec3 L = hdrTestRay.direction;
        vec3 color = hdrColor(L);
        float pdf_light =hdrPdf(L, hdrResolution);
        float pdf_brdf=0.0;
        vec3 f_r = DisneyEval(V, N, L, hit.material,pdf_brdf);
        float mis_weight = misMixWeight(pdf_light, pdf_brdf);
        Lo += mis_weight * history * color * f_r * dot(N, L) / pdf_light;
        //Lo=L*0.5+0.5;
    }
}
*/
//}

bool IsDirectLightVisible(vec3 origin, vec3 direction, float maxDistance)
{
    Ray shadowRay;
    shadowRay.startPoint = origin + direction * 0.0005;
    shadowRay.direction = direction;

    HitResult blocker = hitBVH(shadowRay);
    if (!blocker.isHit) {
        return true;
    }

    if (maxDistance >= INF * 0.5) {
        return false;
    }

    return blocker.hitDistance >= maxDistance - 0.001;
}

vec3 EstimateDirectLighting(HitResult hit, vec3 history, float eta)
{
    LightSample lightSample = SampleOneLight(hit.hitPoint, rand(), rand(), rand());
    if (!lightSample.valid || lightSample.pdf <= 0.0) {
        return vec3(0.0);
    }

    vec3 V = -hit.viewDir;
    vec3 N = hit.normal;
    vec3 L = lightSample.direction;
    float NdotL = abs(dot(N, L));
    if (NdotL <= 0.0) {
        return vec3(0.0);
    }

    if (!IsDirectLightVisible(hit.hitPoint, L, lightSample.distance)) {
        return vec3(0.0);
    }

    float bsdfPdf = 0.0;
    vec3 f = DisneyEval(V, N, L, hit.material, eta, bsdfPdf);
    if (bsdfPdf <= 0.0 || maxComponent(f) <= 0.0) {
        return vec3(0.0);
    }

    float misWeight = lightSample.delta ? 1.0 : misMixWeight(lightSample.pdf, bsdfPdf);
    return history * lightSample.radiance * f * NdotL * misWeight / lightSample.pdf;
}

OutputColor pathTracingImportanceSampling(Ray r, int maxBounce) {
    OutputColor o_c;
    //vec3 Lo = vec3(0);      // 最终的颜色
    vec3 history = vec3(1); // 递归积累的颜色
    o_c.render_color=vec3(0);

    o_c.normal_color=vec3(0);//对于环境光贴图的位置设为0
    o_c.base_color=vec3(0);

    float pdf_brdf = 0.0;
    float NdotL;
    vec3 f_r;

    bool inMedium = false;
    bool mediumSampled = false;
    bool surfaceScatter = false;

    bool log_normal=false;
    for(int bounce=0;; bounce++) {
        
         HitResult newHit = hitBVH(r);

         // 未命中   
         
        if(!newHit.isHit) {
#ifdef USEENVIRONMENTMAP
            vec3 color = hdrColor(r.direction);
            float pdf_light = LightPdf(r.startPoint, r.direction, -1, INF);
            float mis_weight = (bounce > 0 && pdf_brdf > 0.0) ? misMixWeight(pdf_brdf, pdf_light) : 1.0;

            o_c.render_color+=mis_weight * history * color;
#endif   
            break;
        }
        
        // 命中光源积累颜色  
        if (maxComponent(newHit.material.emissive) > 0.0) {
            float pdf_light = LightPdf(r.startPoint, r.direction, newHit.triangleIndex, newHit.hitDistance);
            float mis_weight = (bounce > 0 && pdf_brdf > 0.0) ? misMixWeight(pdf_brdf, pdf_light) : 1.0;
            o_c.render_color+=mis_weight * history *newHit.material.emissive;
        }

       
        mediumSampled = false;
        surfaceScatter = false;

        if(inMedium)
        {        
            if(newHit.material.mediumtype== MEDIUM_ABSORB)
            {
                history *= exp(-(1.0 -newHit.material.mediumColor) * newHit.hitDistance * newHit.material.mediumDensity);
            }
            else if(newHit.material.mediumtype == MEDIUM_EMISSIVE)
            {
                vec3 light_st=newHit.material.mediumColor * newHit.hitDistance * newHit.material.mediumDensity * history;
                
                o_c.render_color+=light_st;
                
            
            }
            else
            {
                // Sample a distance in the medium
                float scatterDist = min(-log(rand()) / newHit.material.mediumDensity, newHit.hitDistance);
                mediumSampled = scatterDist < newHit.hitDistance;

                if (mediumSampled)
                {       
                    if(bounce == maxBounce)//将maxBounce放置在这里是为了maxBounce为1时正确处理透明效果
                        break;
                    history *= newHit.material.mediumColor;

                    // Move ray origin to scattering position
                    r.startPoint += r.direction * scatterDist;
                    //state.fhp = r.origin;

                    // Transmittance Evaluation
                    

                    // Pick a new direction based on the phase function
                    vec3 scatterDir = SampleHG(-r.direction, newHit.material.mediumAnisotropy, rand(), rand());

                    //这里计算一个虚拟法向
//                    if(bounce==0){     
//                        o_c.normal_color=normalize(normalize(scatterDir)-newHit.viewDir);
//                        o_c.normal_color=-newHit.viewDir;                        
//                        o_c.base_color=newHit.material.mediumColor;
//                        o_c.depth_point=newHit.hitPoint;
//                    }
                    //scatterSample.pdf = PhaseHG(dot(-r.direction, scatterDir), state.medium.anisotropy);//在体积散射多重重要性采样里使用
                    r.direction = scatterDir;
                }
            }

        }

        if (!mediumSampled)
        {
            if(newHit.material.alphaMode==ALPHA_MODE_TRANSPARENT){
                //如果是透明的直接穿透 并沿用之前的光线方向
                if(!log_normal){     
                    //o_c.normal_color=normalize(normalize(scatterDir)-newHit.viewDir);
                    o_c.normal_color=-newHit.viewDir;         
                    o_c.base_color=newHit.material.mediumColor;
                    log_normal=true;
                   // o_c.depth_point=newHit.hitPoint;
                }
                bounce--;
            }else{
            
                if(!log_normal){        
                    o_c.normal_color=newHit.normal;
                    o_c.base_color=newHit.material.baseColor;
                    log_normal=true;
                    //o_c.depth_point=newHit.hitPoint;
                 }
                if(bounce == maxBounce)//将maxBounce放置在这里是为了maxBounce为1时正确处理透明效果
                    break;
                surfaceScatter = true;
                vec2 uv;
                uv.x=sobelNumber[bounce*2];
                uv.y=sobelNumber[bounce*2+1];
                uv = CranleyPattersonRotation(uv);
                float xi_1 = uv.x;
                float xi_2 = uv.y; 
                float xi_3 = rand();   
                float eta =newHit.isInside ? newHit.material.IOR:(1.0 / newHit.material.IOR) ;
                vec3 V = -newHit.viewDir;
                vec3 N = newHit.normal;   
                o_c.render_color += EstimateDirectLighting(newHit, history, eta);
                // 采样 BRDF 得到一个方向 L
                vec3 L =  DisneySample(xi_1, xi_2, xi_3, V, N, newHit.material,eta); 
                NdotL =abs(dot(N, L));
                f_r = DisneyEval(V, N, L, newHit.material,eta,pdf_brdf);
                if(pdf_brdf <= 0.0) break;
                history *= f_r * NdotL / pdf_brdf;  // 累积颜色
                r.direction = L;
            }        
            r.startPoint = newHit.hitPoint;        

            //这里至少要单独存储 medium ，之后要引入体积栈
            if(!newHit.isInside &&
            dot(r.direction,newHit.normal)<0 &&
            newHit.material.mediumtype!=MEDIUM_NONE){
                inMedium = true;
            }
            else if(newHit.material.mediumtype!=MEDIUM_NONE){
                inMedium = false;
            }
        }


        // 加入俄罗斯轮盘赌 (关键位置)
        if (bounce >= 3) { // 前3次反弹不启用RR减少噪声
            float rrSurvivalProb = min(0.95, max(maxComponent(history), 0.05)); // 保持最小5%存活率
            if (rand() > rrSurvivalProb) break;     // 终止路径
            history /= rrSurvivalProb;              // 保持无偏
        }
    }
   // return Lo;
   return o_c;
}
