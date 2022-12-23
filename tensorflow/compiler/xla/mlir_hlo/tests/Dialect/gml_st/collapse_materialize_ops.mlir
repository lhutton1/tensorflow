// RUN: mlir-hlo-opt %s --gml-collapse-materialize-ops | \
// RUN: FileCheck %s

func.func @compose_tiles(%arg: tensor<?x?xf32>, %i: index, %j: index, %k: index,
    %n: index, %a: index, %b: index) -> tensor<4x?xf32> {
  %4 = gml_st.materialize %arg[%i, %j] [4, 128] [2, %a]
    : tensor<?x?xf32> to tensor<4x128xf32>
  %5 = gml_st.materialize %4[0, %k] [4, %n] [1, %b]
    : tensor<4x128xf32> to tensor<4x?xf32>
  return %5 : tensor<4x?xf32>
}
// CHECK-LABEL: @compose_tiles
// CHECK-SAME:  %[[ARG:[a-z0-9]+]]: tensor<?x?xf32>, %[[I:[a-z0-9]+]]: index,
// CHECK-SAME:  %[[J:[a-z0-9]+]]: index, %[[K:[a-z0-9]+]]: index,
// CHECK-SAME:  %[[N:[a-z0-9]+]]: index, %[[A:[a-z0-9]+]]: index,
// CHECK-SAME:  %[[B:[a-z0-9]+]]: index)

// CHECK-DAG:  %[[AK:.*]] = arith.muli %[[A]], %[[K]]
// CHECK-DAG:  %[[J_PLUS_AK:.*]] = arith.addi %[[J]], %[[AK]]
// CHECK-DAG:  %[[AB:.*]] = arith.muli %[[A]], %[[B]]
// CHECK-NEXT: %[[RES:.*]] = gml_st.materialize %[[ARG]]
// CHECK-SAME:   [%[[I]], %[[J_PLUS_AK]]] [4, %[[N]]] [2, %[[AB]]]
// CHECK-SAME:   : tensor<?x?xf32>
