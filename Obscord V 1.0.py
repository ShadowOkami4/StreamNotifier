#Welcome to my Obs to Discord Webhook Skript plugin 
#By Okami
#My Twitch https://www.twitch.tv/shadowokami_
#My Youtube https://www.youtube.com/@shadowokami04

import obspython as obs
from discord_webhook import DiscordWebhook, DiscordEmbed




def script_description():
	return "Welcome to OBScord a OBS Python Skript\nThat sends a Discord Webhook Message Embeded or not when you start your Stream\nHope you like it\nOkami"



def script_properties():
    props = obs.obs_properties_create()
    textline = obs.OBS_TEXT_DEFAULT
    textbox = obs.OBS_TEXT_MULTILINE

    obs.obs_properties_add_text(props,"webhook_url","Webhook URL",textline)
    obs.obs_properties_add_text(props,"content","Message",textbox)
    obs.obs_properties_add_text(props,"author","Author",textline)
    obs.obs_properties_add_text(props,"author_url","Author URL",textline)
    obs.obs_properties_add_text(props,"author_icon_url","Author Icon URL",textline)
    obs.obs_properties_add_text(props,"title","Title",textline)
    obs.obs_properties_add_text(props,"description","Description",textbox)
    obs.obs_properties_add_text(props,"body_url","Body Url", textline)
    obs.obs_properties_add_color(props,"color", "Color(Hex):")
    obs.obs_properties_add_text(props,"field1_name", "Field 1 Title",textline)
    obs.obs_properties_add_text(props,"field1_value","Field 1 Description",textbox)
    obs.obs_properties_add_text(props,"field2_name", "Field 2 Title",textline)
    obs.obs_properties_add_text(props,"field2_value","Field 2 Description",textbox)
    obs.obs_properties_add_text(props,"image_url","Image Url",textline)
    obs.obs_properties_add_text(props,"thumbnail_url","Thumbnail URL",textline)
    obs.obs_properties_add_text(props,"footer","Footer",textline)
    return props



def script_update(settings):
    global webhook_url
    global enabled
    global content
    global author
    global author_url
    global author_icon_url
    global title
    global description
    global body_url
    global color
    global field1_name
    global field1_value
    global field2_name
    global field2_value
    global image_url
    global thumbnail_url
    global footer
    

    webhook_url = obs.obs_data_get_string(settings, "webhook_url").strip()
    enabled = validate_webhook_url(webhook_url)
    content = obs.obs_data_get_string(settings, "content")
    author = obs.obs_data_get_string(settings, "author")
    author_url = obs.obs_data_get_string(settings, "author_url").strip()
    author_icon_url = obs.obs_data_get_string(settings, "author_icon_url").strip()
    title = obs.obs_data_get_string(settings, "title")
    description = obs.obs_data_get_string(settings, "description")
    body_url = obs.obs_data_get_string(settings, "body_url").strip()
    color = obs.obs_data_get_int(settings, "color")
    field1_name = obs.obs_data_get_string(settings, "field1_name")
    field1_value = obs.obs_data_get_string(settings, "field1_value")
    field2_name = obs.obs_data_get_string(settings, "field2_name")
    field2_value = obs.obs_data_get_string(settings, "field2_value")
    image_url = obs.obs_data_get_string(settings, "image_url").strip()
    thumbnail_url = obs.obs_data_get_string(settings, "thumbnail_url").strip()
    footer = obs.obs_data_get_string(settings, "footer")

    
    
    
    if not enabled:
	    print("Invalid Webhook URL : {}".format(webhook_url))


def frontend_event_handler(data):
    if data == obs.OBS_FRONTEND_EVENT_STREAMING_STARTED and enabled:
        webhook=  DiscordWebhook(url=webhook_url, content=content)
        embed = DiscordEmbed(title=title, description=description,url=body_url,)
        embed.set_color(((color & 0xFF) << 16) | (color & 0xFF00) | ((color & 0xFF0000) >> 16))
        embed.set_author(name=author,url=author_url,icon_url=author_icon_url,) 
        embed.add_embed_field(name=field1_name, value=field1_value)
        embed.add_embed_field(name=field2_name, value=field2_value)
        embed.set_image(url=image_url)
        embed.set_thumbnail(url=thumbnail_url)
        embed.set_footer(text=footer)
        webhook.add_embed(embed)
        webhook.execute()
        











def validate_webhook_url(url):
    return url.startswith("https://discord.com/api/webhooks/")


obs.obs_frontend_add_event_callback(frontend_event_handler)
