import std.io
import std.ml.tensor
import std.ml.nn
import std.ml.autodiff as ad
import std.ml.optim

struct Params {
    w1: Tensor<f32, [784, 128]>,
    w2: Tensor<f32, [128, 10]>,
}

fn model<const N: usize>(x: Tensor<f32, [N, 784]>, p: &Params) -> Tensor<f32, [N, 10]> {
    let h = nn.relu(x @ p.w1)
    return nn.softmax(h @ p.w2)
}

fn train_step<const N: usize>(
    x: Tensor<f32, [N, 784]>,
    y: Tensor<f32, [N, 10]>,
    params: &mut Params,
    opt: &mut optim.Sgd,
) {
    let (loss, grads) = ad.value_and_grad(params, |p| {
        return nn.cross_entropy(model(x, p), y)
    })
    opt.step(params, grads)
    io.println("loss = #{loss}")
}
