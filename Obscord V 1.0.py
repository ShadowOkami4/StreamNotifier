#Welcome to my Obs to Discord Webhook Skript plugin 
#By Okami
#My Twitch https://www.twitch.tv/shadowokami_
#My Youtube https://www.youtube.com/@shadowokami04

import obspython
from discord_webhook import DiscordWebhook, DiscordEmbed

#Grundvariabeln
webhook_url=""
embed = False
enabled = False
content=""
#Embedvariabln
#Author:
author=""
author_url=""
author_icon_url=""
#Body:
title=""
description=""
body_url=""
color=0
#Field 1:
field1_name=""
field1_value=""
#Field 2:
field2_name=""
field2_value=""
#Images:
image_url=""
thumbnail_url=""
#Footer:
footer=""


def script_update(settings):
 #Grundvariabeln
    global webhook_url
    global embed
    global enabled
    global content
 #Embedvariabln
    #Author:
    global author
    global author_url
    global author_icon_url
    #Body:
    global title
    global description
    global body_url
    global color
    #Field 1:
    global field1_name
    global field1_value
    #Field 2:
    global field2_name
    global field2_value
    #Images:
    global image_url
    global thumbnail_url
    #Footer:
    global footer
    
 #Grundvariabeln  
    webhook_url = obspython.obs_data_get_string(settings, "webhook_url").strip()
    embed = obspython.obs_data_get_bool(settings, "embed")
    enabled = validate_webhook_url(webhook_url)
    content = obspython.obs_data_get_string(settings, "content")
 #Embedvariabln
    #Author:
    author = obspython.obs_data_get_string(settings, "author")
    author_url = obspython.obs_data_get_string(settings, "author_url").strip()
    author_icon_url = obspython.obs_data_get_string(settings, "author_icon_url").strip()
    #Body:
    title = obspython.obs_data_get_string(settings, "title")
    description = obspython.obs_data_get_string(settings, "description")
    body_url = obspython.obs_data_get_string(settings, "body_url").strip()
    color = obspython.obs_data_get_int(settings, "color")
    #Field 1:
    field1_name = obspython.obs_data_get_string(settings, "field1_name")
    field1_value = obspython.obs_data_get_string(settings, "field1_value")
    #Field 2:
    field2_name = obspython.obs_data_get_string(settings, "field2_name")
    field2_value = obspython.obs_data_get_string(settings, "field2_value")
    #Images:
    image_url = obspython.obs_data_get_string(settings, "image_url").strip()
    thumbnail_url = obspython.obs_data_get_string(settings, "thumbnail_url").strip()
    #Footer:
    footer = obspython.obs_data_get_string(settings, "footer")

    
    
    
    if not enabled:
	    print("Invalid Webhook URL : {}".format(webhook_url))
            
def script_description():
	return "Welcome to OBScord a OBS Python Skript\nThat sends a Discord Webhook Message Embeded or not when you start your Stream\nHope you like it\nOkami"

def script_properties():
    props = obspython.obs_properties_create()
    #Grundvariabeln  
    obspython.obs_properties_add_text(props,
                                      "webhook_url",
                                      "Webhook URL",
                                      obspython.OBS_TEXT_DEFAULT)
    obspython.obs_properties_add_text(props,
                                      "content",
                                      "Content",
                                      obspython.OBS_TEXT_MULTILINE)
    obspython.obs_properties_add_bool(props,
                                      "embed",
                                      "Embed")
    #Embedvariabln
    #Author
    obspython.obs_properties_add_text(props,
                                      "author",
                                      "Author",
                                      obspython.OBS_TEXT_DEFAULT)
    obspython.obs_properties_add_text(props,
                                      "author_url",
                                      "Author URL",
                                      obspython.OBS_TEXT_DEFAULT)
    obspython.obs_properties_add_text(props,
                                      "author_icon_url",
                                      "Author Icon URL",
                                      obspython.OBS_TEXT_DEFAULT)
    #Body
    obspython.obs_properties_add_text(props,
                                      "title",
                                      "Title",
                                      obspython.OBS_TEXT_DEFAULT)
    obspython.obs_properties_add_text(props,
                                      "description",
                                      "Description",
                                      obspython.OBS_TEXT_MULTILINE)
    obspython.obs_properties_add_text(props,
                                      "body_url",
                                      "Body Url",
                                      obspython.OBS_TEXT_DEFAULT)
    

    obspython.obs_properties_add_int_slider(props,
                                            "color",
                                            "Color",
                                            0,
                                            16777215,
                                            1)
    #Field 1:
    obspython.obs_properties_add_text(props,
                                      "field1_name",
                                      "Field 1 Name",
                                      obspython.OBS_TEXT_DEFAULT)
    obspython.obs_properties_add_text(props,
                                      "field1_value",
                                      "Field 1 Value",
                                      obspython.OBS_TEXT_MULTILINE)
    #Field 2:
    obspython.obs_properties_add_text(props,
                                      "field2_name",
                                      "Field 2 Name",
                                      obspython.OBS_TEXT_DEFAULT)
    obspython.obs_properties_add_text(props,
                                      "field2_value",
                                      "Field 2 Value",
                                      obspython.OBS_TEXT_MULTILINE)
    #Images:
    obspython.obs_properties_add_text(props,
                                      "image_url",
                                      "Image Url",
                                      obspython.OBS_TEXT_DEFAULT)
    obspython.obs_properties_add_text(props,
                                      "thumbnail_url",
                                      "Thumbnail URL",
                                      obspython.OBS_TEXT_DEFAULT)
    #Footer:
    obspython.obs_properties_add_text(props,
                                      "footer",
                                      "Footer",
                                      obspython.OBS_TEXT_DEFAULT)
    return props



def frontend_event_handler(data):
    if data == obspython.OBS_FRONTEND_EVENT_STREAMING_STARTED and enabled:
        if embed:
             embedweb =  DiscordWebhook(url=webhook_url, content=content)
             embed1 = DiscordEmbed(title=title, description=description,url=body_url, color=color)
             embed1.set_author(name=author,url=author_url,icon_url=author_icon_url,) 
             embed1.add_embed_field(name=field1_name, value=field1_value)
             embed1.add_embed_field(name=field2_name, value=field2_value)
             embed1.set_image(url=image_url)
             embed1.set_thumbnail(url=thumbnail_url)
             embed1.set_footer(text=footer)

             embedweb.add_embed(embed1)
             embedweb.execute()
        else:
        
        
            webhook = DiscordWebhook(url=webhook_url, content=content)
            webhook.execute()
        











def validate_webhook_url(url):
    return url.startswith("https://discord.com/api/webhooks/")


obspython.obs_frontend_add_event_callback(frontend_event_handler)