#include <Components.hpp>
#include <Handle.hpp>
#include <MathUtilities.hpp>
#include <MultiFieldComponent.hpp>
#include <Type.hpp>
#include <tuple>


struct SomeNewType
{
	int x;
	int y;
	int z;
};


struct TestData
{
	int								hello;
	int								world;
	float							imAfloat;
	double							imADouble;
	FlexKit::float4					vector;
	FlexKit::Vector<SomeNewType>	dynArrayType;
};

constexpr FlexKit::ComponentID TestComponentID = GetTypeGUID(TestData);

using TestComponentHandle	= FlexKit::Handle_t<32, TestComponentID>;
using TestComponent			= FlexKit::BasicComponent_t<TestData, TestComponentHandle, TestComponentID>;

constexpr FlexKit::ComponentID ComplexComponentID = GetTypeGUID(ComplexComponentID);
using ComplexComponentHandle	= FlexKit::Handle_t<32, TestComponentID>;
using TestMultiFieldComponent	= FlexKit::MultiFieldComponent_t<ComplexComponentHandle, ComplexComponentID, FlexKit::MultiFieldComponentEventHandler, TestData, TestData>;
