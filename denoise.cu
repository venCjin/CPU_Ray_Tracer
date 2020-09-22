#include "denoise.h"
#include <iostream>
#include <cuda.h>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>

const int KERNEL_DIM = 3;

__device__ bool pixel_cmp(uchar3& a, uchar3& b)
{
    return (a.x + a.y + a.z > b.x + b.y + b.z);
}

__device__ uchar3 median(uchar3 vector[], int count)
{
    // bubble sort
    for (int i = 0; i < count; ++i)
    {
        for (int j = 0; j < count - i; ++j)
        {
            if (pixel_cmp(vector[j], vector[j + 1]))
            {
                uchar3 temp = vector[j];
                vector[j] = vector[j + 1];
                vector[j + 1] = temp;
            }
        }
    }

    // find median value
    int index = count / 2;
    /*if (count % 2 == 0)
    {
        uchar3 median_value;
        median_value.x = (vector[index].x + vector[index + 1].x) / 2;
        median_value.y = (vector[index].y + vector[index + 1].y) / 2;
        median_value.z = (vector[index].z + vector[index + 1].z) / 2;
        return median_value;
    }*/
    return vector[index];
}

__global__ void denoise_kernel(uchar3* input_image, uchar3* output_image, int width, int height)
{
    const unsigned int offset = blockIdx.x * blockDim.x + threadIdx.x;
    int x = offset % width;
    int y = (offset - x) / width;

    if (offset < width * height)
    {
        float r = 0, g = 0, b = 0;
        int count = 0;
        uchar3 vector[KERNEL_DIM * KERNEL_DIM];

        for (int ox = -(KERNEL_DIM / 2); ox < (KERNEL_DIM / 2) + 1; ox++)
        {
            for (int oy = -(KERNEL_DIM / 2); oy < (KERNEL_DIM / 2) + 1; oy++)
            {
                if ((x + ox) > -1 && (x + ox) < width && (y + oy) > -1 && (y + oy) < height)
                {
                    const int current_offset = offset + ox + oy * width;

                    vector[count].x = input_image[current_offset].x;
                    vector[count].y = input_image[current_offset].y;
                    vector[count].z = input_image[current_offset].z;

                    ++count;
                }
            }
        }
        output_image[offset] = median(vector, count);
    }
}

void getError(cudaError_t err)
{
    if (err != cudaSuccess)
    {
        std::cout << "Error " << cudaGetErrorString(err) << std::endl;
    }
}

void denoise(unsigned char* input_image, unsigned char* output_image, int width, int height)
{
    unsigned char* dev_input;
    unsigned char* dev_output;
    getError(cudaMalloc((void**)&dev_input, width * height * 3 * sizeof(unsigned char)));
    getError(cudaMemcpy(dev_input, input_image, width * height * 3 * sizeof(unsigned char), cudaMemcpyHostToDevice));

    getError(cudaMalloc((void**)&dev_output, width * height * 3 * sizeof(unsigned char)));

    dim3 blockDims(512, 1, 1);
    dim3 gridDims(ceil(double(width * height) / blockDims.x), 1, 1);

    denoise_kernel<<<gridDims, blockDims>>>((uchar3*)dev_input, (uchar3*)dev_output, width, height);

    getError(cudaMemcpy(output_image, dev_output, width * height * 3 * sizeof(unsigned char), cudaMemcpyDeviceToHost));

    getError(cudaFree(dev_input));
    getError(cudaFree(dev_output));
}
