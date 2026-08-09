import torch
import torch.nn as nn
import os


class TinyNet(nn.Module):
    def __init__(self, num_classes=2):
        super().__init__()
        self.features = nn.Sequential(
            nn.Conv2d(3, 4, kernel_size=3, stride=2, padding=1),
            nn.ReLU(),
            nn.AdaptiveAvgPool2d((1, 1))
        )
        self.fc = nn.Linear(4, num_classes)

    def forward(self, x):
        x = self.features(x)
        x = x.view(x.size(0), -1)
        return self.fc(x)


def main():
    model = TinyNet()
    model.eval()
    dummy = torch.randn(1, 3, 224, 224)
    out = model(dummy)
    print('output shape', out.shape)

    fixtures_dir = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(fixtures_dir, 'dummy_classifier.onnx')
    torch.onnx.export(
        model,
        dummy,
        path,
        input_names=['input'],
        output_names=['output'],
        dynamic_axes=None,
        opset_version=11
    )
    print('saved', path, os.path.getsize(path))


if __name__ == '__main__':
    main()
