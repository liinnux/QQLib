<?php
namespace QQAPI;

include_once "application.inc"; //It needs Application::$POST to have been initialized, but
    //otherwise is fairly standalone.
include_once "util.inc";

/**
* Does almost all the work for a route to handle image upload, as well as deletion,
* order changing, show/hide, editing associated data such as caption, etc.
*
* It is configured with an options array, given to the constructor. Most keys are described below.
*
* The sub-commands are:
*       new: (the default): A file is being uploaded
*       order: A new order is being given
*       delete: Remove the media file
*       show/hide: Add or remove the "hidden" tag
*       data: Alter data. Currently you can set 'caption' and/or set 'tag'.
*           If tag is an array it replaces the existing list of tags.
*           If a string beginning with "+" it adds a tag, and if "-" it removes a string.
*           (A string not beginning with either is treated like "+"; blank string is quietly ignored.)
*
* You can allow only a subset of those commands by altering $options['supportedActions'].
*
* One essential input is 'data'. This is an array where info on each image will be kept.
* You normally give this by reference, and then save it after calling process().
* The array index is the display order. (I.e. when the order of the first two images is swapped,
* we actually swap them around in this array.) The array values are associative arrays, with
* a number of keys:
*   type:  'image/jpeg'
*   filename:  This is the filenamePrefix, plus a filename, but without the .jpg extension.
*   versions: An array of strings, each string is e.g. '320x240'. If blank then it is assumed
*       resized versions are not being made, and the file should be taken from /img/uploaded/ directly.
*   caption: Description of the image. i18n (see below)
*   tag: An array of strings.
*       The special tag is "hidden", which is used for the show/hide functionality.
*   receivedTime: Unix time (either integer or floating-point)
*   receivedSize: The size of the uplodaed file
*   receivedFilename: The original filename, as given by the user.
*       NOTE: the original file is kept under "img/uploaded/", but uses 'filename' even there.
*       Warning: receivedFilename should be treated as untrusted user input.
*
* An i18n string can be a simple string or an array of
* strings, where the keys in the array are two-letter language codes.
* There is also the special value of "_", which is used to mark a field as deliberately
* empty. (This can be used either with the simple string form, or the array form.)
* The point of that is that "" strings can be hilighted to the user as needing to be set,
* which can be especially useful for translation; "_" can be used to make such prompting
* go quiet.

* Other option keys available:
*   'directory': The base directory for storing images. There will be one sub-directory called
*       'uploaded' where the raw images are kept, then one sub-directory for each thumbnail size.
*       Sub-directories are created the first time they are needed.
*       There is no default: setting this variable implies you will be using file system storage.
*       NOTE: should end in a forward slash (unless doing something deliberate).
*   'now': Defaults to microtime(true) (Normally only given in unit tests, or if you already have
*        this value and want it consistent throughout processing a route.)
*   'accept': An array of mime types to allow. If not given it defaults to array('image/jpeg').
*   'autoRotate': true (the default) means if it is a jpeg look at exif data to see if it needs rotating.
*       Ignored for non-jpegs.
*   'thumbnails': This is an array of image sizes to make. If blank then no resized versions will
*       be made (and 'versions', in the per-image 'data' will be blank) and the originally uploaded
*       file will be used.
*       The image sizes are "WWxHH". When the original image does not match that aspect
*       ratio an image will be cut-out.
*       Note: the string is also used as the directory name.
*       NOTE: only the first entry is always made. The others are only made if the original
*       image is big enough. E.g. if thumbnails is array('320x200','800x600'), then two images will
*       be made from '1024x768', but only the 320x200 image will be made from a source image that
*       is 640x480. And if the source image is 160x100 then the 320x200 image is still made, but will
*       be stretched.
*       @todo I'd like to support "WWx" and "xHH" formats, which would mean aim for that width
*           or height, but keep the original images aspect ratio. We'd then have /img/800x/ as the
*           the directory name, etc.
*   'thumbnailMimeType': What type of file to make. The choices here depend on
*       the library used, so indirectly depend on the source mime type.
*       For GD we currently support:
*           image/jpeg:  .jpg file
*           image/png:  .png file
*           image/gif:  .gif file
*           (For anything not recognized, image/jpeg is used, rather than fail.)
*       @TODO: describe for PDF
*   'filenamePrefix': Optional prefix on all images. This is typically something to identify the user.
*       If it contains "/" then sub-directories will be created. E.g. "123/" means the 320x200 will
*       be found at   /img/320x200/123/abcdef.jpg
*       NOTE: on data storage methods where there is no concept of sub-directories, the "/" will
*           just be part of the filename.
*       NOTE: you could alternatively embed userID in 'directory' rather than here. Then the above
*           example URL might be:  /img/123/320x200/abcdef.jpg
*           or even: /123images/320x200/abcdef.jpg
*       @todo Support for making sub-directories when a "/" is in the filenamePrefix is not there yet...
*   'supportedActions': Array of valid values for action. You can set this to a subset (e.g. if show/hide is
*       not to be supported). NOTE: if you allow an action that this class does not support then
*       a SystemException will be thrown.
*
* @todo I think I'd like some options to control: a) if 320x200 is always made; b) if all
*     thumbnail sizes should always be made, even if it means stretching. I.e. then we could
*     say 800x600 would be made even if the source was 640x480.
*       ---> Alternatively how about have both 'thumbnails' and 'resize'. The latter are
*           sizes we always make, even if it involves making it bigger, and the former are
*           only made for making it smaller. Then to get the current behaviour of always
*           320x200, but only 800x600 if source is bigger, we'd do: 'resize'=>array('320x200'), 'thumbnails'=>array('800x600'). (If a size is found in both, quietly ignore the one in thumbnails.)
*
* @todo Think how to support alternative image data storage methods, beyond just local
*    filesystem. The most obvious one is an SQL database. In that case maybe we take a PDOWrapper
*    object as a config option; also the name of the table to use, and the field names.
*    Another very useful one would be Amazon S3.
*       ---> Bear in mind that PHP requires files to be copied locally before they can be moved to S3 (?)
*    IMPORTANT: We don't want everything under the sun in this class, so I guess we
*           need some plugin structure. That could be as simple as deriving from this base class
*           for the storage method being chosen.
*   NOTE: we may want this to tie in with the storage method for the 'data' too?
*
* @todo I wonder if the data_filtering code for media objects should be moved here (e.g. as
*       a public static function).
*
* @internal The key for all changes is the filename, rather than the index into 'data'.
*   This is deemed more stable. (e.g. if requests come in at almost the same time, to
*   change order, then delete one: by using filename we make sure the correct item is deleted. From
*   another point of view, of multiple edits, the worst case is that the order of changes is not the
*   desired order; if we used the index the worst case is that the changes get made on the wrong image.)
*
* @internal NOTE: PHP does session locking. That implies we can never have two edit actions running
*   concurrently.
*
* @internal Originally I had two classes, one for image upload, one for everything else. And I'd
*    wondered about creating a class for each of the other actions. But there was no strong need
*    and one big class works well too.
*
* @internal 'show/myimg.jpg' could be implemented as 'data/myimg.jpg/tag/-hidden'
*		and 'hide/myimg.jpg' could be implemented as 'data/myimg.jpg/tag/hidden'
*   (ignoring the fact that the data is POST-ed, not in the URL.)
*   I think the cleaner interface is justification enough, but also it allows changing
*   the internal representation.
*/
class MediaHandler{
/** See docs for class to explain what keys are here */
private $options;

/** 
* Note: 'data' and 'directory' are required elements. This will throw if
* they are not given.
*/
function __construct($options){
$this->options = array_replace_recursive( array(
    'thumbnails' => array('320x200', '800x600'),  //800x600 only made if source image is big enough.
    'filenamePrefix' => '',
    'now' => microtime(true),
    'accept'=>array('image/jpeg'),
    'supportedActions' => array('new','order','show','hide','delete','data'),
    'autoRotate'=>true,

    //Some params that control thumbnails being made
    'thumbnailMimeType'=>'image/jpeg',  //Make jpegs by default
    'jpegQuality' => 75,    //0 to 100
    'jpegUseInterlace' => false,
    'pngQuality' => null,  //0..9; -1 uses zlib default, which is apparently 6 by default. I'm hoping null gives default.
    'pngFilters' => null,   //TODO: No idea what default is, or even when to use this!

    ), $options );

if(!array_key_exists('data', $this->options) || !is_array($this->options['data']))throw new SystemException("data is required in options, and must be an array");

if(!array_key_exists('directory', $this->options) || !is_string($this->options['directory']))throw new SystemException("directory is required in options, and must be a string");   //TODO: this might get shifted down to a derived class, that handles file system

}

/**
*
* @todo IMPORTANT I think for the standalone demo we will need a 'get' action too, which
*    will return 'data' ?
*
* @todo Image not being physically deleted yet.
*
* @return Mixed This depends on action. For most actions there is no return.
*     Note: returning from this function is considered success; all errors throw.
*/
public function process(){
$action = Application::getString('action','new');   //If action not given then assume a file upload
if(!in_array($action, $this->options['supportedActions']))throw new ErrorException('Unknown action. Must be one of: %1$s', array(implode(', ',$this->options['supportedActions'])),"action=$action");

if($action == 'new')return $this->newItem();

if($action == 'order')return $this->reorderMedia();

$filename = Application::getString('filename');
$ix = $this->findByFilename($filename);

switch($action){
    case 'show':
        array_simple_remove_value($this->options['data'][$ix]['tag'],'hidden');
        break;

    case 'hide':
        //Add the 'hidden' tag.
        if(!in_array('hidden',$this->options['data'][$ix]['tag'])){
            $this->options['data'][$ix]['tag'][]='hidden';
            }
        break;

    case 'delete':
        //TODO: remove the images from disk, too.
        //--> Perhaps just move the original form to a "deleted" directory, that can be archived.
        //     At the same time, append an entry to "deleted_images.log", which is basically the
        //     entry in media that we will be deleting with the next line.
        unset($this->options['data'][$ix]);
        break;

    case 'data':
        if(array_key_exists('caption',Application::$POST)){
            $caption = Application::get('caption');  //Either a string or an array of 'en' and 'ja' elements
            //TODO: some validation?
            $this->options['data'][$ix]['caption'] = $caption;
            }
        if(array_key_exists('tag',Application::$POST)){
            $tag = Application::get('tag'); //Expect array or string
            if(is_array($tag))$this->options['data'][$ix]['tag'] = $tag;    //Complete replacement
            elseif($tag=='');   //Ignore blank string
            elseif($tag{0}=="-")array_simple_remove_value($this->options['data'][$ix]['tag'], substr($tag,1));
            else{
                if($tag{0}=="+")$tag=substr($tag,1);
                if(!in_array($tag,$this->options['data'][$ix]['tag']))$this->options['data'][$ix]['tag'][] = $tag;   //Add if not already there
                }
            }
        
        break;

    default:
        throw new SystemException("Unexpected action ($action)");
    }

return null;
}


/**
*/
private function reorderMedia(){
$order = Application::getArray('order');    //I.e. throws if not an array with 1+ elements
if(count($order) != count($this->options['data']))throw new ErrorException("order array is wrong size.",array(),"order size=".count($order).", expected ".count($this->options['data'])." elements");
$newMedia = array();
foreach($order as $newIx => $filename){
    $foundIx = $this->findByFilename($filename);    //Throws if unknown filename
    $newMedia[$newIx] = $this->options['data'][$foundIx];
    }
$this->options['data'] = $newMedia;
return null;
}


/**
* Converts filename to an index. (Throws if not found.)
*
* @internal Does this belong in utility.inc? OTOH, will it ever be used outside this route?
*
* @internal I originally checked the $item['type'] matches too (e.g. image/jpeg).
*   This has been removed as I cannot think of a situation where this check actually
*   catches anything useful in this version.
*/
private function findByFilename($filename){
foreach($this->options['data'] as $ix=>$item){
    if($item['filename'] != $filename)continue;
    return $ix;
    }
throw new ErrorException("Bad/unknown filename.",array(),"filename=$filename");
}


/**
* For a new image being uploaded
*
* @return Number The index of the newly added item.
*     @todo If we started supported multiple uploads in one go then I guess this
*           would return an array of indexes.
*/
private function newItem(){
if(!isset($_FILES) || !array_key_exists('files',$_FILES) || !array_key_exists('error',$_FILES['files']))throw new ErrorException("File data missing.");
if(count($_FILES["files"]['error'])!=1)throw new ErrorException("Zero or multiple files uploaded. Only exactly one allowed.",array(),"count=".count($_FILES["files"]['error'])); 

foreach ($_FILES["files"]["error"] as $key => $error) {
    switch($error){
        case 0:return $this->processOneNewImage($key);    //NB. this is handled in derived class
        case 1:case 2:
            throw new ErrorException("Uploaded file is too big",array(),"key=$key,error code=$error, ini setting=".ini_get('upload_max_filesize') );
        case 3:case 4:throw new ErrorException("Some problem with uploaded file",array(),"key=$key,error code=$error");
        default:throw new SystemException("key=$key,error code=$error (6:tmp dir missing; 7: cannot write to disk; 8: extension stopped upload)");
        }
    }
}


/**
* Helper for newItem()
*
* @param String $key The entry in $_FILES['files'][*] being processed.
* @return Number The index of the newly added item.
*
* @internal Though we have the structure here for accepting multiple uploads,
*   in fact this is only called once, in the current design.
*
* @todo That 6-digit could be customizable? Or, better, maybe we should make it hex, or even base-26 or base-36?
*
* @todo The mkdir mode of 0750 could be customizable.
* @todo Consider if any of this could be moved to a base class.
*      Perhaps the bit where we create $entry?
* @internal Using realpath() is probably not needed. Adding the file_exists() check
*    was more important. But realpath() should do no harm, so has been left in.
*/
protected function processOneNewImage($key){
$uploadDirectory = realpath($this->options['directory'].'uploaded');
if(!file_exists($uploadDirectory) && !mkdir($uploadDirectory,0750,/*recursive*/true))throw new SystemException("Couldn't create uploadDirectory ($uploadDirectory)");

$mimeType = $_FILES['files']['type'][$key];

//This is for firefox bug #373621. Not really needed for other browsers.
//  (The Firefox bug actually affects all mime-types, but for the file types we
//      accept it is really only ever going to be an issue with PDFs.)
if(preg_match('/(.pdf)$/i', $_FILES['files']['name'][$key]))$mimeType="application/pdf";

if(!in_array($mimeType, $this->options['accept']))throw new ErrorException("Unsupported media type.",array(),"MimeType=$mimeType (need to add to options['accept'] if should be supported)."); 

switch($mimeType){
    case 'image/jpeg':$ext = '.jpg';break;
    case 'image/png':$ext = '.png';break;
    case 'image/gif':$ext = '.gif';break;
    case 'image/bmp':$ext = '.bmp';break;
    case 'image/webp':$ext = '.webp';break;
    case 'application/pdf':case 'application/x-pdf':$ext = '.pdf';break;
    //TODO: could add other mime types we allow uploads of, even if thumbnail generation not supported.
    default:throw new ErrorException("Unsupported media type.",array(),"MimeType=$mimeType (not supported by the code, but in the accept list!)"); 
    }
 
for($tries=0;$tries<30;++$tries){
    $randomNumber = mt_rand(1,999999);
    $newFname = $this->options['filenamePrefix'].sprintf('%06d',$randomNumber);
    $fname = $uploadDirectory.'/'.$newFname.$ext;
    if(!file_exists($fname))break;
    mt_srand(microtime(true)*1000000);
    }
if(file_exists($fname))throw new SystemException("Couldn't find a unique filename even (in {$this->options['directory']}uploaded/) after {$tries} tries. Giving up.");

if(!move_uploaded_file($_FILES['files']['tmp_name'][$key], $fname)){
    throw new SystemException('Failed to process upload file (fname=$fname)');
    }

//Make thumbnail(s)
$versions = $this->makeSmallerImages($fname, $mimeType, $newFname);

//Prepare entry in data array
$entry=array(
    'type'=>$this->options['thumbnailMimeType'],
    'srcType'=>$mimeType,
    'filename'=>$newFname,
    'versions'=>$versions,
    'caption'=>'',
    'tag'=>array('hidden'),    //Start new images off hidden
    'receivedTime'=>$this->options['now'],
    'receivedSize'=>$_FILES['files']['size'][$key],
    'receivedFilename'=>$_FILES['files']['name'][$key],
    //TODO: record IP address, user agent, etc of the user who uploaded it?
    );

//DEBUG("Made entry,data is currently:");DEBUG($this->options['data']);  //TEMP

//$nextIx=count($this->options['data']);    //Doesn't cope if a gap in the data array.
$d = @array_keys($this->options['data']);
$nextIx = count($d)==0 ? 0 : (max($d) + 1);

$this->options['data'][$nextIx]=$entry;

//DEBUG("nextIx = $nextIx ,data is now:");DEBUG($this->options['data']);  //TEMP

return $nextIx;
}


/**
* Creates the thumbnail(s)
*
* NOTE: hard-coded to only output jpeg thumbnails. But that could be changed in future fairly easily.
*
* Supports all the various input formats (handed off to other functions where necessary)
* or quietly returns without making anything when not supported. It also makes all
* the output thumbnail sizes.
*
* Note: if file(s) already exist they are overwritten. See processOneNewImage() for
* why this should never happen.
*
* @param String $fname The full-size original file (in the "uploaded" directory)
* @return Array The list of image sizes made.

TODO:
Generalize this to take the desired width,height.
Also take a parameter that says force or not. We will use force for 320x240, but not 800x600.
If the desired width/height is bigger in either dimension than the source image, return false and make nothing. Unless force is true, in which case go ahead and make it anyway!
---> See todos in the class comments, for similar discussion

@todo IMPORTANT: 320x240 is assumed. I.e. $this->options['thumbnails'] is ignored
@todo CRITICAL: The below code assumes the 4:3 aspect ratio???!!!!
* @todo The mkdir mode of 0750 could be customizable.
*
* @param String $srcMimeType The mime type of $fname. If not supported this function
*       returns without making anything.
*       If image/jpeg,png,gif,bmp,webp then GD is used.
*       If application/pdf then ?????
*
* @internal The GD installed check not done until after we call imagecreatefromXXX
*   which is why we do error-suppression on that call.
*   This is done so that srcMimeTypes that don't need GD don't cause a complaint
*   about it not being installed.
*/
private function makeSmallerImages($fname, $srcMimeType, $newFname){
switch($srcMimeType){
    case 'application/pdf':case 'application/x-pdf':
        return $this->makePDFThumbnail($fname,$newFname);
        break;
    case 'image/jpeg':$sourceImage = @imagecreatefromjpeg($fname);break;
    case 'image/png':$sourceImage = @imagecreatefrompng($fname);break;
    case 'image/gif':$sourceImage = @imagecreatefromgif($fname);break;
    case 'image/bmp':$sourceImage = @imagecreatefrombmp($fname);break;
    case 'image/webp':$sourceImage = @imagecreatefromwebp($fname);break;
    default:return array(); //Quietly do nothing for unsupported source types
    }

if(!function_exists('imagecreatetruecolor')) {
    throw new SystemException("imagecreatetruecolor not found. Is GD extension installed?");
    }
if(!$sourceImage){
    throw new SystemException("GD extension appears to be present, but imagecreatefromXXX failed. Unsupported mime type? Bad input file?");
    }

$width = imagesx( $sourceImage );
$height = imagesy( $sourceImage );

$w=320;$h=240;

$directory = realpath($this->options['directory'].$w.'x'.$h);
if(!file_exists($directory) && !mkdir($directory,0750,/*recursive*/true))throw new SystemException("Couldn't create thumbs directory ($directory)");

if($this->options['autoRotate'] && $srcMimeType=='image/jpeg'){
    $exif = @exif_read_data($fname);
    if($exif){
        $orientation = intval(@$exif['Orientation']);
        //2..8 means some action needs to be taken
        //It seems only 3, 6 and 8 need a fix? (2,4,5 and 7 are "flipped", apparently uncommon)
        //1 is Top-left.
        //2 is Top-right, 4 is Bottom-left, 5 is Left-Top, 7 is Right-Bottom.
        switch($orientation){
            case 3: //Bottom-right
                $sourceImage = imagerotate($sourceImage,180,0);
                break;
            case 6: //Right-top
                $sourceImage = imagerotate($sourceImage,-90,0);
                break;
            case 8: //Left-bottom    
                $sourceImage = imagerotate($sourceImage,90,0);
                break;
            }
        }
    }

$img = imagecreatetruecolor($w,$h);

$width43 = (int)(($height/3)*4);
$height43 = (int)(($width/4)*3);
if($width43 < $width){  //This means it is wider than a 4:3, so we need an x-offset
    $x = (int)(($width - $width43)/2);
    imagecopyresized( $img, $sourceImage, 0, 0, $x, 0, $w, $h, $width43, $height );
    }
else{   //Either it is 4:3, or it is higher than a 4:3, so we need a y-offset
    $y = (int)(($height - $height43)/2);
    imagecopyresized( $img, $sourceImage, 0, 0, 0, $y, $w, $h, $width, $height43 );
    }

$saveFname = $directory.'/'.$newFname;  //NOTE: the file extension added in below switch.
switch($this->options['thumbnailMimeType']){
    case 'image/png':
        imagepng($img, $saveFname.".png", $this->options['pngQuality'], $this->options['pngFilters']);
        break;
    case 'image/gif':
        imagegif($img, $saveFname.".gif");
        break;
    default:case 'image/jpeg':
        if($this->options['jpegUseInterlace'])imageinterlace($img,true);
        imagejpeg($img, $saveFname.".jpg", $this->options['jpegQuality']);
        break;
    }

imagedestroy($sourceImage);

return array("320x240");
}


/**
*
* @todo 320x240 hard-coded
* @todo A "imagick installed?" check needed
*
* @todo Add better support for multi-page PDFs.
*/
function makePDFThumbnail($fname,$newFname){
$w=320;$h=240;

$im = new \imagick($fname); //With no [0] it does the last page by default
if($im->getNumberImages()>=2){
    $im->setiteratorindex(0);   //Just do first page
    }
$im->thumbnailImage($w,$h,/*bestfit=*/true,/*fill=*/true);  //bestfit means it keeps the aspect
    //ratio of the original; fill then means it will pad (with white) on either side so that the image
    //is still $w x $h pixels.

$directory = realpath($this->options['directory'].$w.'x'.$h);
if(!file_exists($directory) && !mkdir($directory,0750,/*recursive*/true))throw new SystemException("Couldn't create thumbs directory ($directory)");

$saveFname = $directory.'/'.$newFname;  //NOTE: the file extension added in below switch.
switch($this->options['thumbnailMimeType']){
    case 'image/png':$saveFname.='.png';break;
    case 'image/gif':$saveFname.='.gif';break;
    default:case 'image/jpeg':$saveFname.='.jpg';break;
    //TODO: what other formats are supported?
    }
$im->writeImage($saveFname);

return array("320x240");
}



}   //End of class MediaHandler
