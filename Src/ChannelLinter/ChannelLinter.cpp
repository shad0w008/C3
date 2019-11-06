#include "StdAfx.h"
#include "Core/Profiler.h"

namespace MWR::C3::Linter
{
	void OutputDebug(std::string_view output)
	{
#ifdef _DEBUG
		std::cout << output << std::endl;
#endif // _DEBUG
	}

	/// @throws std::runtime_error if channel with given name was not registered
	auto const& GetChannelInfo(std::string_view channelName)
	{
		return InterfaceFactory::Instance().Find<AbstractChannel>(channelName)->second;
	}

	/// template to avoid typing the whole name
	template<class T>
	auto GetChannelCapability(T const& channelInfo)
	{
		try
		{
			auto ret =  json::parse(channelInfo.m_Capability);
			C3::Core::Profiler::Gateway::EnsureCreateExists(ret);
			C3::Core::Profiler::Gateway::AddBuildInCommands(ret, true);
			return ret;
		}
		catch (json::parse_error& e)
		{
			throw std::runtime_error("Failed to parse channel's capability json. "s + e.what());
		}
	}

	/// template to avoid typing the whole name
	template<class T>
	auto MakeDevice(MWR::json const& createParams, const T& chInfo)
	{
		auto blob = MWR::C3::Core::Profiler::TranslateArguments(createParams);
		auto channelBridge = std::make_shared<C3::Linter::MockDeviceBridge>(chInfo.m_Builder(blob));
		channelBridge->OnAttach();
		return channelBridge;
	}
}

#ifdef _DEBUG
#define DebugDump(x) MWR::C3::Linter::OutputDebug(x)
#else
#define DebugDump(x)
#endif // _DEBUG

/// Entry point of the application.
/// @param argc number of program arguments.
/// @param argv vector of program arguments.
int main(DWORD argc, char* argv[])
try
{
	using namespace MWR;

	std::cout << "Custom Command and Control - Channel linter. BUILD: " << C3_BUILD_VERSION << std::endl;
	C3::Linter::AppConfig context(argc, argv);
	auto const& config = context.GetConfig();

	// select channel
	auto const& chInfo = C3::Linter::GetChannelInfo(config.m_ChannelName);

	// read create and prompt for arguments
	auto capability = C3::Linter::GetChannelCapability(chInfo);

	std::cout << "Create channel 1" << std::endl;
	C3::Linter::Form form(capability.at("/create/arguments"_json_pointer));
	auto createParams = form.FillForm(config.m_ChannelArguments);
	auto channel = C3::Linter::MakeDevice(createParams, chInfo);

	if (config.m_TestChannelIO)
	{
		std::cout << "Create channel 2" << std::endl;
		auto const& ch2Args = config.m_ComplementaryChannelArguments ? *config.m_ComplementaryChannelArguments : form.GetComplementaryArgs(config.m_ChannelArguments);
		json createParams2 = form.FillForm(ch2Args);
		auto ch2 = C3::Linter::MakeDevice(createParams2, chInfo);

		//  test write and read
		auto data = ByteVector(ByteView(MWR::Utils::GenerateRandomString(64)));
		channel->GetDevice()->OnSendToChannelInternal(data);
		auto rcv = std::static_pointer_cast<C3::AbstractChannel>(ch2->GetDevice())->OnReceiveFromChannelInternal();
		if (data != rcv.at(0))
			throw std::exception("data sent and received mismatch");
	}

	if (config.m_Command)
	{
		auto& commandParams = *config.m_Command;
		auto commandIdL = std::stoi(commandParams.at(0));
		auto commandId = static_cast<uint16_t>(commandIdL);

		auto& commands = capability.at("commands");
		auto commandIt = std::find_if(begin(commands), end(commands), [commandId](auto const& c) { return c.contains("id") && c["id"].get<uint16_t>() == commandId;});
		if (commandIt == end(commands))
			throw std::runtime_error("Failed to find a command with id: " + std::to_string(commandId));

		C3::Linter::Form commandForm(commandIt->at("arguments"));
		auto args = commandForm.FillForm({begin(commandParams) + 1, end(commandParams) }); // +1 to omit command ID
		auto x = ByteVector{}.Concat(commandId, C3::Core::Profiler::TranslateArguments(args));
		channel->RunCommand(x);
	}
}
catch (std::exception & e)
{
	std::cerr << e.what() << std::endl;
	return 1;
}
