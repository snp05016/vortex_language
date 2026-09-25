; When may a compiler turn four f32 loads into one 16-byte load?
;
; A lane that loads four neighbouring floats with one 16-byte instruction
; issues a quarter of the load instructions. PTX requires the address of such
; a load to be a multiple of 16, so the compiler must prove that first. LLVM's
; load-store vectorizer merges neighbouring loads into vector loads; its source
; says NVIDIA and AMD GPUs motivated it, and both of their back ends run it.
;
; Run: opt -S -passes=infer-alignment,load-store-vectorizer vector_loads.ll
;
; infer-alignment works out each load's alignment from the global's alignment
; and the getelementptr arithmetic. Element [i, 4q] of @square, a 64 x 64
; array, sits 256 * i + 16 * q bytes in: a multiple of 16, so the four loads
; become one <4 x float> load. A row of @padded, a 32 x 33 tile, is 132 bytes
; long, so element [i, 4q] is only known to be 4-byte aligned and the loads
; stay scalar. The padding that removes bank conflicts from a column read
; takes 16-byte alignment away from a row read.
;
; The file names no target, so opt applies LLVM's generic rules, which never
; accept a misaligned vector access.

@square = global [64 x [64 x float]] zeroinitializer, align 16
@padded = global [32 x [33 x float]] zeroinitializer, align 16

; Returns square[i, 4q .. 4q + 3].
define <4 x float> @quad_square(i64 %i, i64 %q) {
  %j0 = shl nuw nsw i64 %q, 2
  %j1 = add nuw nsw i64 %j0, 1
  %j2 = add nuw nsw i64 %j0, 2
  %j3 = add nuw nsw i64 %j0, 3
  %p0 = getelementptr inbounds [64 x [64 x float]], ptr @square, i64 0, i64 %i, i64 %j0
  %p1 = getelementptr inbounds [64 x [64 x float]], ptr @square, i64 0, i64 %i, i64 %j1
  %p2 = getelementptr inbounds [64 x [64 x float]], ptr @square, i64 0, i64 %i, i64 %j2
  %p3 = getelementptr inbounds [64 x [64 x float]], ptr @square, i64 0, i64 %i, i64 %j3
  %a0 = load float, ptr %p0, align 4
  %a1 = load float, ptr %p1, align 4
  %a2 = load float, ptr %p2, align 4
  %a3 = load float, ptr %p3, align 4
  %v0 = insertelement <4 x float> poison, float %a0, i64 0
  %v1 = insertelement <4 x float> %v0, float %a1, i64 1
  %v2 = insertelement <4 x float> %v1, float %a2, i64 2
  %v3 = insertelement <4 x float> %v2, float %a3, i64 3
  ret <4 x float> %v3
}

; Returns padded[i, 4q .. 4q + 3].
define <4 x float> @quad_padded(i64 %i, i64 %q) {
  %j0 = shl nuw nsw i64 %q, 2
  %j1 = add nuw nsw i64 %j0, 1
  %j2 = add nuw nsw i64 %j0, 2
  %j3 = add nuw nsw i64 %j0, 3
  %p0 = getelementptr inbounds [32 x [33 x float]], ptr @padded, i64 0, i64 %i, i64 %j0
  %p1 = getelementptr inbounds [32 x [33 x float]], ptr @padded, i64 0, i64 %i, i64 %j1
  %p2 = getelementptr inbounds [32 x [33 x float]], ptr @padded, i64 0, i64 %i, i64 %j2
  %p3 = getelementptr inbounds [32 x [33 x float]], ptr @padded, i64 0, i64 %i, i64 %j3
  %a0 = load float, ptr %p0, align 4
  %a1 = load float, ptr %p1, align 4
  %a2 = load float, ptr %p2, align 4
  %a3 = load float, ptr %p3, align 4
  %v0 = insertelement <4 x float> poison, float %a0, i64 0
  %v1 = insertelement <4 x float> %v0, float %a1, i64 1
  %v2 = insertelement <4 x float> %v1, float %a2, i64 2
  %v3 = insertelement <4 x float> %v2, float %a3, i64 3
  ret <4 x float> %v3
}
