#include <iostream>
#include <thread>

#include "rtweekend.h"
#include "hittable_list.h"
#include "sphere.h"
#include "camera.h"
#include "material.h"

#include "lodepng.h"
#include "Instrumentor.h"


#define DENOISE 0
#if DENOISE
#include "denoise.h"
#endif

color ray_color(const ray& r, const hittable& world, int depth) {
	hit_record rec;

	// If we've exceeded the ray bounce limit, no more light is gathered.
	if (depth <= 0)
		return color(0, 0, 0);

	if (world.hit(r, 0.001, infinity, rec)) {
		//point3 target = rec.p + rec.normal + random_in_unit_sphere();
		//point3 target = rec.p + rec.normal + random_unit_vector();
		//point3 target = rec.p + random_in_hemisphere(rec.normal);
		//return 0.5 * ray_color(ray(rec.p, target - rec.p), world, depth - 1);
		ray scattered;
		color attenuation;
		if (rec.mat_ptr->scatter(r, rec, attenuation, scattered))
			return attenuation * ray_color(scattered, world, depth - 1);
		return color(0, 0, 0);
	}

	vec3 unit_direction = unit_vector(r.direction());
	auto t = 0.5*(unit_direction.y + 1.0);
	return (1.0 - t)*color(1.0, 1.0, 1.0) + t * color(0.5, 0.7, 1.0);
}

struct Pixel
{
	unsigned char r, g, b;
};

hittable_list random_scene() {
	hittable_list world;

	auto ground_material = make_shared<lambertian>(color(0.5, 0.5, 0.5));
	world.add(make_shared<sphere>(point3(0, -1000, 0), 1000, ground_material));

	for (int a = -11; a < 11; a++) {
		for (int b = -11; b < 11; b++) {
			auto choose_mat = random_double();
			point3 center(a + 0.9 * random_double(), 0.2, b + 0.9 * random_double());

			if ((center - point3(4, 0.2, 0)).length() > 0.9) {
				shared_ptr<material> sphere_material;

				if (choose_mat < 0.8) {
					// diffuse
					auto albedo = color::random() * color::random();
					sphere_material = make_shared<lambertian>(albedo);
					world.add(make_shared<sphere>(center, 0.2, sphere_material));
				}
				else if (choose_mat < 0.95) {
					// metal
					auto albedo = color::random(0.5, 1);
					auto fuzz = random_double(0, 0.5);
					sphere_material = make_shared<metal>(albedo, fuzz);
					world.add(make_shared<sphere>(center, 0.2, sphere_material));
				}
				else {
					// glass
					sphere_material = make_shared<dielectric>(1.5);
					world.add(make_shared<sphere>(center, 0.2, sphere_material));
				}
			}
		}
	}

	auto material1 = make_shared<dielectric>(1.5);
	world.add(make_shared<sphere>(point3(0, 1, 0), 1.0, material1));

	auto material2 = make_shared<lambertian>(color(0.4, 0.2, 0.1));
	world.add(make_shared<sphere>(point3(-4, 1, 0), 1.0, material2));

	auto material3 = make_shared<metal>(color(0.7, 0.6, 0.5), 0.0);
	world.add(make_shared<sphere>(point3(4, 1, 0), 1.0, material3));

	return world;
}

// Image
const double aspect_ratio = 16.0 / 9.0;
const int width = 1920;// 1920;
const int height = static_cast<int>(width / aspect_ratio);
const int samples_per_pixel = 400;// 200;
const int max_depth = 16;
Pixel* image = new Pixel[width * height];

// World
auto world = random_scene();

// Camera
point3 lookfrom(13, 2, 3);
point3 lookat(0, 0, 0);
vec3 vup(0, 1, 0);
auto dist_to_focus = 10.0;
auto aperture = 0.1;
camera cam(lookfrom, lookat, vup, 20, aspect_ratio, aperture, dist_to_focus);

void render_pixel(const int j)
{
	PROFILE_SCOPE();
	for (int i = 0; i < width; ++i)
	{
		color pixel_color(0, 0, 0);
		for (int s = 0; s < samples_per_pixel; ++s)
		{
			auto u = double(i + random_double()) / (width - 1);
			auto v = double(j + random_double()) / (height - 1);
			ray r = cam.get_ray(u, v);
			pixel_color += ray_color(r, world, max_depth);
		} // write_color()
		pixel_color.r = sqrt(pixel_color.r / samples_per_pixel);
		pixel_color.g = sqrt(pixel_color.g / samples_per_pixel);
		pixel_color.b = sqrt(pixel_color.b / samples_per_pixel);

		// Write the translated [0,255] value of each color component
		unsigned index = ((height - 1 - j) * width) + i;
		//unsigned index = (j * width) + i;
		image[index].r = static_cast<unsigned char>(256 * clamp(pixel_color.r, 0.0, 0.999));
		image[index].g = static_cast<unsigned char>(256 * clamp(pixel_color.g, 0.0, 0.999));
		image[index].b = static_cast<unsigned char>(256 * clamp(pixel_color.b, 0.0, 0.999));
	}
}

int main(int argc, char** argv)
{
	std::vector<std::thread> workers;
	workers.reserve(height);

	// Render
	#define MULTI_THREAD_RUN 0
	#if MULTI_THREAD_RUN
	Instrumentor::Instance().beginSession("Render", "multi_core_run.json");
	#else
	Instrumentor::Instance().beginSession("Render", "single_core_run.json");
	#endif
	{
		PROFILE_SCOPE();
		for (int j = 0; j < height; ++j)
		{
			printf("\rProgress: %3.0f%%", double(j + 1) / height * 100);
			#if MULTI_THREAD_RUN
			workers.push_back(std::thread(render_pixel, j));
			#else
			render_pixel(j);
			#endif
		}
		#if MULTI_THREAD_RUN
		printf("\n");
		unsigned c = 0;
		for (auto& w : workers)
		{
			printf("\rProgress: %d", c++);
			w.join();
		}
		#endif
	}
	Instrumentor::Instance().endSession();


#if DENOISE
	// cuda denoise filter
	denoise((unsigned char*)image, (unsigned char*)image, width, height);
#endif

	const char* file = "out.png";
	unsigned int error = lodepng::encode(file, (unsigned char *)image, width, height, LCT_RGB);
	if (error) std::cout << "encoder error " << error << ": " << lodepng_error_text(error) << std::endl;

	return 0;
}
